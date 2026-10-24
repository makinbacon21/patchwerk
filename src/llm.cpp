// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#include "llm.h"
#include "settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>

Llm::Llm(QObject *parent) : QObject(parent) {}

void Llm::chat(const QString &system, const QString &user, Done done)
{
    QString base = setting("llm/url").toString();
    while (base.endsWith('/'))
        base.chop(1);
    QNetworkRequest req(QUrl(base + "/chat/completions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(setting("llm/timeout").toInt() * 1000);
    QString key = setting("llm/key").toString();
    if (!key.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + key.toUtf8());

    QJsonObject body{{"model", setting("llm/model").toString()},
                     {"messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", system}},
                                             QJsonObject{{"role", "user"}, {"content", user}}}}};
    QNetworkReply *r = m_nam.post(req, QJsonDocument(body).toJson());
    connect(r, &QNetworkReply::finished, this, [r, done] {
        r->deleteLater();
        QByteArray data = r->readAll();
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        QString text = o["choices"][0]["message"]["content"].toString();
        if (r->error() != QNetworkReply::NoError || text.isEmpty()) {
            QString msg = o["error"]["message"].toString();
            done({}, msg.isEmpty() ? r->errorString() : msg);
            return;
        }
        done(text, {});
    });
}

QString stripFences(QString text)
{
    static const QRegularExpression think(R"(<think>.*?</think>)", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression fence(R"(^\s*```[^\n]*\n(.*)\n```\s*$)", QRegularExpression::DotMatchesEverythingOption);
    text.remove(think);
    auto m = fence.match(text);
    return m.hasMatch() ? m.captured(1) : text.trimmed();
}
