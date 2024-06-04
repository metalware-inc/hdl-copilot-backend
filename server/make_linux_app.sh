#!/bin/bash

# Check for required argument
if [ $# -eq 0 ]; then
    echo "Usage: $0 <arch>"
    echo "arch: x86_64 or arm64"
    exit 1
fi

ARCH=$1
APP_NAME="hdl_copilot_server_app"

# Determine the correct binary based on architecture
if [ "$ARCH" = "x86_64" ]; then
    LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    LINUXDEPLOY_BIN="linuxdeploy-x86_64.AppImage"
elif [ "$ARCH" = "arm64" ]; then
    LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-aarch64.AppImage"
    LINUXDEPLOY_BIN="linuxdeploy-aarch64.AppImage"
else
    echo "Invalid architecture: $ARCH"
    echo "Supported architectures are x86_64 or arm64"
    exit 1
fi

# Download and prepare the Linuxdeploy binary
wget $LINUXDEPLOY_URL
mv $LINUXDEPLOY_BIN /tmp/
chmod +x /tmp/$LINUXDEPLOY_BIN

# Create App Dir and supporting files
mkdir -p $APP_NAME/usr/bin
mkdir -p $APP_NAME/usr/lib
cp ./build/src/hdl_copilot_server $APP_NAME/usr/bin/

# Create a minimal desktop file
cat <<EOF > $APP_NAME/dummy.desktop
[Desktop Entry]
Type=Application
Name=hdl_copilot_server
Icon=dummy
Exec=hdl_copilot_server
Categories=Utility;
EOF

# Create AppRun file
cat <<EOF > $APP_NAME/AppRun
#!/bin/bash
exec "\$APPDIR/usr/bin/hdl_copilot_server" "\$@"
EOF
chmod +x $APP_NAME/AppRun

touch $APP_NAME/dummy.svg

# Create portable binary
/tmp/$LINUXDEPLOY_BIN --appdir $APP_NAME --output appimage --executable $APP_NAME/usr/bin/hdl_copilot_server

