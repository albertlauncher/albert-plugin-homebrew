# Albert plugin: Homebrew

## Features

- Search Homebrew packages
- Item actions:
  - Install/Uninstall
  - Show package info in terminal
  - Open package info in browser
  - Open project website
- The empty query yields an update item.

## Technical notes

Uses `brew formulae` and `brew casks` to get a list of packages to match against,
then calls `brew info` to get the package details.
