pkgname=mpc-qt-custom-git
pkgver=0
pkgrel=1
pkgdesc='Media Player Classic reimplemented with Qt and libmpv, with custom feature branches'
arch=('x86_64')
url='https://github.com/jimzrt/mpc-qt'
license=('GPL-2.0-or-later')
options=('!debug')
depends=('mpv' 'qt6-base' 'qt6-svg')
makedepends=('boost' 'cmake' 'git' 'ninja' 'qt6-tools')
provides=('mpc-qt')
conflicts=('mpc-qt' 'mpc-qt-git')
replaces=('mpc-qt' 'mpc-qt-git')

source=(
    'mpc-qt::git+https://github.com/jimzrt/mpc-qt.git#branch=master'
    'feature-thumbnail::git+https://github.com/jimzrt/mpc-qt.git#branch=feature/thumbnail'
    'feature-dialogue-fix::git+https://github.com/jimzrt/mpc-qt.git#branch=feature/dialogue-fix'
    'feature-hold-to-speed::git+https://github.com/jimzrt/mpc-qt.git#branch=feature/hold-to-speed'
)
b2sums=('SKIP' 'SKIP' 'SKIP' 'SKIP')

prepare() {
    cd "$srcdir/mpc-qt"

    git fetch --no-tags "$srcdir/feature-thumbnail" HEAD
    git merge --no-edit --no-ff FETCH_HEAD -m 'Merge feature/thumbnail'

    git fetch --no-tags "$srcdir/feature-dialogue-fix" HEAD
    git merge --no-edit --no-ff FETCH_HEAD -m 'Merge feature/dialogue-fix'

    git fetch --no-tags "$srcdir/feature-hold-to-speed" HEAD
    git merge --no-edit --no-ff FETCH_HEAD -m 'Merge feature/hold-to-speed'
}

pkgver() {
    cd "$srcdir/mpc-qt"
    local version
    version=$(git describe --tags --long --always --match 'v[0-9]*')
    version=${version#v}
    version=${version//-/.}
    printf '%s\n' "$version"
}

build() {
    cmake -S "$srcdir/mpc-qt" -B "$srcdir/build" -G Ninja \
        -DMPCQT_VERSION="$pkgver" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "$srcdir/build"
}

package() {
    DESTDIR="$pkgdir" cmake --install "$srcdir/build"
}
