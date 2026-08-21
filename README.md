# 21 Questions for Flipper Zero

Think of one of the included objects. Flipper asks exactly 21 adaptive Yes/No/Not Sure questions, then makes its best guess. It scores every possible object at the end, so conflicting answers cannot leave it without an answer.

## Controls

- Intro/result: `OK` starts a game.
- Question: `LEFT` = No, `OK` = Not Sure, `RIGHT` = Yes.
- Guess: `LEFT` = No, `RIGHT` = Yes.
- `BACK` exits.

## Automatic GitHub build

Every push to `main` runs `.github/workflows/build.yml`. When the build finishes, open the workflow run and download the `twenty-one-questions-fap` artifact.

The workflow uses Node.js 24-compatible GitHub Actions versions to avoid the Node.js 20 deprecation warning from the earlier project.

## Build locally with uFBT

1. Install Python and uFBT: `python -m pip install --upgrade ufbt`
2. Open a terminal in this folder.
3. Run `ufbt`.
4. Copy the generated `.fap` from `dist` to `SD Card/apps/Games/` on the Flipper Zero.

## Current starter database

The included build contains 30 objects and 15 adaptive questions. Add more objects by assigning combinations of the trait flags in `twenty_one_questions.c`.
