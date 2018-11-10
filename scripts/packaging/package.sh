#!/usr/bin/env bash

set -e          # Exit on errors
set -o pipefail # Unveils hidden failures

SCRIPT_DIR="$( cd "$(dirname "$0")" ; pwd -P )"
PROJECT_DIR="$( cd "${SCRIPT_DIR}/../.." ; pwd -P )"
PROJECT_DIR_NAME=${PROJECT_DIR##*/}
BUILD_DIR="${PROJECT_DIR}/build"
BUILD_OS=${1:-}
PACKAGE_TYPE=${2:-}

cd "${SCRIPT_DIR}"
source ../utils.sh
source check_dependencies.sh

if [ ! -f "/etc/debian_version" ]; then
	print_critical "Package script only works on Debian/Ubuntu systems!"
fi

print_help() {
	echo ""
	echo "Usage: ./package_release.sh (linux|win) package-type [options]"
	echo ""
	echo "This script builds and packages MediaElch in a distributable form."
	echo "It is used by MediaElch authors to release official MediaElch releases."
	echo ""
	echo "    DO NOT USE THIS IF YOU'RE AN END-USER"
	echo ""
	echo "Use build_release.sh if you want to build MediaElch."
	echo ""
	echo "Package Types:"
	echo "  linux"
	echo "    AppImage  Create a linux app image. See https://appimage.org/"
	echo "    deb       Build an unsigned deb package. Only for testing or"
	echo "              distributing test-branches."
	echo "    launchpad Upload a new MediaElch version to launchpad.net. You need a "
	echo "              clean repository for that (no build files)."
	echo "              Note: Set your signing key in $ME_SIGNING_KEY."
	echo "  win"
	echo "    zip       Create a ZIP file containing MediaElch and its dependencies."
	echo ""
	echo "Options"
	echo "  --no-confirm   Package MediaElch without confirm dialog."
	echo ""
}

# Gather information for packaging MediaElch such as version, git
# hash and date.
gather_information() {
	pushd "${PROJECT_DIR}" > /dev/null

	VERSION_FULL=$(sed -ne 's/.*AppVersionStr[^"]*"\(.*\)";/\1/p' Version.h)    # Format: 2.4.3-dev
	VERSION=$(echo $VERSION_FULL | sed -e 's/-dev//')                           # Format: 2.4.3
	GIT_VERSION_FULL=$(git describe --abbrev=12 | sed -e 's/-g.*$// ; s/^v//')  # Format: 2.4.3-123
	GIT_VERSION=$(echo $GIT_VERSION_FULL | sed -e 's/-/./')                     # Format: 2.4.3
	GIT_REVISION=$(echo $GIT_VERSION_FULL | sed -e 's/.*-// ; s/.*\..*//')      # Format: 123
	GIT_DATE=$(git --git-dir=".git" show --no-patch --pretty="%ci")
	# RELEASE_DATE=$(date -u +"%Y-%m-%dT%H:%M:%S%z" --date="${GIT_DATE}")
	GIT_HASH=$(git --git-dir=".git" show --no-patch --pretty="%h")
	DATE_HASH=$(date -u +"%Y-%m-%d_%H-%M")
	VERSION_NAME="${VERSION}_${DATE_HASH}_git-${TRAVIS_BRANCH}-${GIT_HASH}"

	if [ -z "$GIT_REVISION" ]; then
		GIT_REVISION="1" # May be empty
	fi

	print_important "Information used for packaging:"
	echo "  Git Version         = ${GIT_VERSION}"
	echo "  Git Revision        = ${GIT_REVISION}"
	echo "  Git Date            = ${GIT_DATE}"
	#echo "  Release Date      = ${RELEASE_DATE}"
	echo "  Git Hash            = ${GIT_HASH}"
	echo "  Date Hash           = ${DATE_HASH}"
	echo "  MediaElch Version   = ${VERSION}"
	echo "  Version Name (Long) = ${VERSION_NAME}"

	if [[ ! $GIT_VERSION == $VERSION ]]; then
		echo ""
		print_error    "Git version and MediaElch version do not match!"
		print_error    "Add a new Git tag using:"
		print_critical "  git tag -a v1.1.0 -m \"Version 1.1.0\""
	fi

	popd > /dev/null
}

confirm_build() {
	echo ""
	print_important "Do you want to package MediaElch ${VERSION} for ${BUILD_OS} with these settings?"
	print_important "It is recommended to clean your repository using \"git clean -fdx\"."
	read -s -p  "Press enter to continue, Ctrl+C to cancel"
	echo ""
}

# Creates an .AppImage file that can be distributed.
package_appimage() {
	check_dependencies_linux_appimage

	$SCRIPT_DIR/build_release.sh linux

	pushd "${BUILD_DIR}" > /dev/null
	echo ""
	print_important "Create an AppImage release"

	# Workaround for: https://github.com/probonopd/linuxdeployqt/issues/65
	unset QTDIR; unset QT_PLUGIN_PATH; unset LD_LIBRARY_PATH
	# linuxdeployqt uses $VERSION this for naming the file

	if [[ ! "$PATH" = *"qt"* ]]; then
		print_critical "/path/to/qt/bin must be in your \$PATH, e.g. \nexport PATH=\"\$PATH:/usr/lib/x86_64-linux-gnu/qt5/bin\""
	fi

	if [[ "$(qmlimportscanner)" = *"could not find a Qt installation"* ]]; then
		print_critical "qmlimportscanner could not find a Qt installation.\nInstall qtdeclarative5-dev-tools\"."
	fi

	#######################################################
	# Download linuxdeployqt

	echo ""
	print_info "Downloading linuxdeployqt"
	DEPLOYQT="${PROJECT_DIR}/linuxdeployqt.AppImage"
	if [ ! -f $DEPLOYQT ]; then
		wget --output-document "${PROJECT_DIR}/linuxdeployqt.AppImage" \
			https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage
	fi
	chmod u+x $DEPLOYQT

	#######################################################
	# Install MediaElch into subdirectory

	echo ""
	print_info "Installing MediaElch in subdirectory to create basic AppDir structure"
	make INSTALL_ROOT=appdir -j $(nproc) install
	find appdir/

	#######################################################
	# Copy libmediainfo
	# 
	# libmediainfo.so.0 is loaded at runtime that's why
	# linuxdeployqt can't detect it and we have to include
	# it here.

	echo ""
	print_info "Copying libmediainfo.so"
	mkdir -p ./appdir/usr/lib
	cp /usr/lib/x86_64-linux-gnu/libmediainfo.so.0 ./appdir/usr/lib/

	#######################################################
	# Download and copy ffmpeg

	echo ""
	print_info "Downloading ffmpeg"
	# Use static ffmpeg
	wget -c https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-64bit-static.tar.xz -O ffmpeg.tar.xz
	tar -xJvf ffmpeg.tar.xz
	print_info "Copying ffmpeg into AppDir"
	cp ffmpeg-*/ffmpeg appdir/usr/bin/

	#######################################################
	# Create AppImage

	echo ""
	print_important "Creating an AppImage for MediaElch ${VERSION_NAME}. This takes a while and may seem frozen."
	$DEPLOYQT appdir/usr/share/applications/MediaElch.desktop -verbose=1 -bundle-non-qt-libs -qmldir=../src/ui
	$DEPLOYQT --appimage-extract
	export PATH=$(readlink -f ./squashfs-root/usr/bin):$PATH
	# Workaround to increase compatibility with older systems
	# see https://github.com/darealshinji/AppImageKit-checkrt for details
	mkdir -p appdir/usr/optional/libstdc++/
	wget -c https://github.com/darealshinji/AppImageKit-checkrt/releases/download/continuous/exec-x86_64.so -O ./appdir/usr/optional/exec.so
	cp /usr/lib/x86_64-linux-gnu/libstdc++.so.6 ./appdir/usr/optional/libstdc++/
	rm appdir/AppRun
	wget -c https://github.com/darealshinji/AppImageKit-checkrt/releases/download/continuous/AppRun-patched-x86_64 -O appdir/AppRun
	chmod a+x appdir/AppRun
	./squashfs-root/usr/bin/appimagetool -g ./appdir/ MediaElch-${VERSION}-x86_64.AppImage
	find . -executable -type f -exec ldd {} \; | grep " => /usr" | cut -d " " -f 2-3 | sort | uniq

	#######################################################
	# Finalize AppImage (name, chmod)

	echo ""
	print_info "Renaming .AppImage"
	mv MediaElch-${VERSION}*.AppImage MediaElch_linux_${VERSION_NAME}.AppImage
	chmod +x *.AppImage

	print_success "Successfully created AppImage: "
	print_success "    $(pwd)/MediaElch_linux_${VERSION_NAME}.AppImage"
	popd > /dev/null
}

prepare_deb() {
	echo ""
	check_dependencies_linux_deb

	cd "${PROJECT_DIR}/.."

	# For Debian packaging, we need a distrinct version for each upload
	# to launchpad. We can achieve this by using the git revision.
	VERSION="${VERSION}.${GIT_REVISION}"
	
	# Create target directory
	TARGET_DIR=mediaelch-${VERSION}
	rm -rf ${TARGET_DIR} && mkdir ${TARGET_DIR}

	print_info "Copying sources to ./${TARGET_DIR}"
	(cd $PROJECT_DIR_NAME; tar cf - . ) | (cd ${TARGET_DIR}; tar xf - )

	pushd ${TARGET_DIR}  > /dev/null

	# Remove untracked files but keep changes
	git clean -fdx
	# .git is ~20MB, so remove it
	rm -rf scripts obs travis-ci documentation .git

	# A bit of useful information
	echo $GIT_HASH > .githash
	echo $GIT_DATE > .gitdate
	echo $GIT_VERSION > .gitversion
	
	# New Version; Get rivision that is not in changelog
	PPA_REVISION=0
	while [ $PPA_REVISION -le "99" ]; do
		PPA_REVISION=$(($PPA_REVISION+1))
		if [[ ! $(grep $VERSION-$PPA_REVISION debian/changelog) ]]; then
			break
		fi
	done
	
	# Create changelog entry
	print_info "Adding new entry for version ${VERSION}-${PPA_REVISION} in"
	print_info "debian/changelog using information from debian/control"
	dch -v $VERSION-$PPA_REVISION~xenial -D xenial -M -m "next nightly build"
	cp debian/changelog ${PROJECT_DIR}/debian/changelog

	popd > /dev/null

	print_info "Create source file (.tar.gz)"
	tar ch "${TARGET_DIR}" | xz > "mediaelch_${VERSION}.orig.tar.xz"
}

# Creates an unsigned deb.
package_deb() {
	prepare_deb

	print_info "Run debuild"
	pushd ${TARGET_DIR} > /dev/null
	debuild -uc -us
	popd > /dev/null
}

# Uploads a new MediaElch release to launchpad.net
package_and_upload_to_launchpad() {
	echo ""
	print_important "Upload a new MediaElch Version to https://launchpad.net"

	if [ -z "$ME_SIGNING_KEY" ]; then
		print_critical "\$ME_SIGNING_KEY is empty or not set! Must be a GPG key id"
	fi

	prepare_deb
	
	print_info "Run debuild"
	pushd ${TARGET_DIR} > /dev/null
	debuild -k${ME_SIGNING_KEY} -S
	
	# Create builds for other Ubuntu releases that Launchpad supports
	distr=xenial # Ubuntu 16.04
	others="trusty bionic cosmic" # Ubuntu 14.04 17.10 and 18.10
	for next in $others
	do
		sed -i "s/${distr}/${next}/g" debian/changelog
		debuild -k${ME_SIGNING_KEY} -S
		distr=$next
	done

	popd > /dev/null

	pushd "${PROJECT_DIR}/.." > /dev/null
	dput ppa:bugwelle/mediaelch-test mediaelch_${VERSION}-${PPA_REVISION}~*.changes
	popd > /dev/null
}

pkg_type="$(lc ${PACKAGE_TYPE:-invalid})"
no_confirm=${3:-confirm}

if [ "${BUILD_OS}" == "linux" ] ; then
	if [ $pkg_type != "appimage" ] && [ $pkg_type != "deb" ] && [ $pkg_type != "launchpad" ]; then
		print_error "Unknown package type for linux: \"${PACKAGE_TYPE}\""
		print_help
		exit 1
	fi

	gather_information
	[ "${no_confirm}" != "--no-confirm" ] && confirm_build
	echo ""

	if [ $pkg_type == "appimage" ]; then
		package_appimage ${no_confirm}
	elif [ $pkg_type == "launchpad" ]; then
		package_and_upload_to_launchpad
	elif [ $pkg_type == "deb" ]; then
		package_deb
	fi

elif [ "${BUILD_OS}" == "win" ]; then
	print_info "Windows build not supported, yet!"
	exit 1

else
	print_error "Unknown OS \"${BUILD_OS}\""
	print_help
	exit 1
fi

