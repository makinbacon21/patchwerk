// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Thomas Makin

#pragma once

#include <QNetworkAccessManager>
#include <functional>

class Llm : public QObject {
    Q_OBJECT
public:
    using Done = std::function<void(const QString &reply, const QString &error)>;

    explicit Llm(QObject *parent = nullptr);
    void chat(const QString &system, const QString &user, Done done);

private:
    QNetworkAccessManager m_nam;
};

QString stripFences(QString text);
