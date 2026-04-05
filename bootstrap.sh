APP="app"
APP_BOOT="app.boot"
COMPILER="clang++ --std=c++20 -Wno-braced-scalar-init"
echo "Bootstrapping ..."
$COMPILER "$APP.cpp" -o "$APP_BOOT" &&
"./$APP_BOOT" build &&
echo "Cleaning ..."  &&
rm "$APP_BOOT"  &&
echo "Bootstrapping DONE." &&
"./$APP" help