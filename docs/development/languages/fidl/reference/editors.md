# FIDL editors

Several editors have support for FIDL:

* [Atom](#atom)
* [IntelliJ/Android Studio](#intellij)
* [Sublime Text](#sublime)
* [Vim](#vim)
* [Visual Studio Code](#visual-studio-code)

## Atom {#atom}

To install:

```
pushd ~/.atom/packages
git clone https://fuchsia.googlesource.com/atom-language-fidl
popd
```

## IntelliJ / Android Studio {#intellij}

There is an IntelliJ plugin available for FIDL. It adds syntax and parsing
support.To install it, select **Settings**, then **Plugins**, and then click
**Browse Repositories** and search for **FIDL**.

## Sublime Text {#sublime}

[Sublime syntax highlighting support](/garnet/public/lib/fidl/tools/sublime).

To install, select **Sublime Text**, then **Preferences**, then
**Browse Packages** and copy the files `FIDL.sublime-syntax`, and
`Comments (FIDL).tmPreferences` into the **User** package.

## Vim {#vim}

[Vim syntax highlighting support and instructions](/garnet/public/lib/fidl/tools/vim).

## Visual Studio Code {#visual-studio-code}

There is a an extension,
[Visual Studio Code extension available](https://marketplace.visualstudio.com/items?itemName=fuchsia-authors.language-fidl).

## Contributing

Contributions to the various plugins are welcome. Their respective code is in:

* [Atom](https://fuchsia.googlesource.com/atom-language-fidl/)
* [IntelliJ](https://fuchsia.googlesource.com/intellij-language-fidl/)
* [Sublime](/garnet/public/lib/fidl/tools/sublime)
* [vim](/garnet/public/lib/fidl/tools/vim)
* [Visual Studio Code](https://fuchsia.googlesource.com/vscode-language-fidl/)

