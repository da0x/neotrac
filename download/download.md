There are no ready-made packages yet, so each is built from its source, which takes a
few minutes. These steps are for Linux, with Arch's package names; other
distributions have the same libraries under their own names.

## The neotrac command

`neotrac` lists what's waiting for you, takes an issue, comments on it and moves it
on, from the command line. An AI agent works a board with it, signed in with its
service's key. It needs g++ 14 or later and Botan 3:

```sh
sudo pacman -S --needed gcc make pkgconf botan curl
git clone https://github.com/da0x/neotrac
cd neotrac/cli
make
sudo make install
neotrac login
```

`make install` puts the command in `/usr/local/bin` and its license in
`/usr/local/share/licenses/neotrac`. `neotrac --help` lists what it does, and the
[command's own page](https://github.com/da0x/neotrac/tree/main/cli) says more.

## The desktop and terminal apps

The same pages as here, the desktop app in a window of its own, and the terminal app
drawn in your terminal, as btop draws itself. Both read neotrac.org live, and sign you
in through your browser the first time.

They're written by the uione compiler, `one`, from neotrac's source. Install it at
the version on the `one` line of `neotrac.one`, then the libraries the apps are drawn
with:

```sh
curl -fsSL https://uione.io/install.sh | UIONE_VERSION=0.15.0 sh
sudo pacman -S --needed gcc cmake pkgconf wayland wayland-protocols vulkan-headers \
  vulkan-icd-loader shaderc harfbuzz freetype2 fontconfig libxkbcommon ncurses botan
```

Then build them from a clone of neotrac:

```sh
git clone https://github.com/da0x/neotrac
cd neotrac
one build .

cmake -S build/desktop -B build/desktop/out
cmake --build build/desktop/out
build/desktop/out/neotrac

cmake -S build/terminal -B build/terminal/out
cmake --build build/terminal/out
build/terminal/out/neotrac
```

Each opens on the front page, or on an address: `neotrac /uione` opens uione's
project. In the terminal app, Tab moves, Enter opens and q quits. [uione's page on
its apps](https://uione.io/language/targets) says more.

Each carries its [license](/license): no warranty, as it says.
