# Patchwerk

Inline GUI review of Linux mailing list patches, in native C++ QT.

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/patchwerk [<message-id> | <lore URL> | <mbox> | <project.json>]
```

Install to `/usr/local` (binary, `.desktop` file and icon):

```sh
cmake -S . -B build && make -C build && sudo make -C build install
```

Needs Qt 6.8+, `git` and `b4`. Sending uses `git send-email` by default (package `git-email`).

## Use

- **Open Series** fetches the thread with `b4 mbox`. The project goes to `~/.local/share/patchwerk/`.
- The current session (series, drafts, open tabs, layout) is kept in `~/.cache/patchwerk/` and restored when you start without arguments.
- Double-click a diff line to draft a comment. Double-click a comment to reply to it, or a draft to edit it.
- **Fetch Latest** fetches the thread again. Auto refresh does this on a timer and shows an alert. It never changes your tree.
- **Apply Series** runs `b4 shazam`. If `git am` stops, **LLM Rebase** asks the LLM to apply the current patch, then stages it.
- A kernel tree is optional. It is needed for Apply Series, LLM Rebase, whole-file view and `get_maintainer.pl`. Click the tree name in the bottom bar to choose one.
- **Pull Tree** finds the subsystem trees for the touched files in the upstream `MAINTAINERS`. It clones the one you pick (blobless), or fetches it into your current tree as `patchwerk/base`.
- **Review** sends the current patch to the LLM and adds its findings as draft comments.
- **Send** writes each reply to `outbox/` and runs the send command on it.

LLM features use any OpenAI-compatible `/chat/completions` endpoint (Settings). The default is a local Ollama.

## AI Usage

Dev assisted primarily via Claude Opus 5.5.

## License

MIT. See [LICENSE](LICENSE).
