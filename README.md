# Lua Azure Iot Hub

This is my take on the Azure IotHub Client SDK for Lua.

See the [wiki](https://github.com/billbsing/lua-azure-iot-hub/wiki) for a simple example using lua.


For more information see the  [online manual](https://htmlpreview.github.io/?https://raw.githubusercontent.com/wiki/billbsing/lua-azure-iot-hub/manual.html)


## Build

First you need to get the latest [Azure Iot Hub SDK](https://github.com/Azure/azure-iot-sdks) from git hub. 
Go to the *azure-iot-sdks/c/build_all/linux* folder, then run the `build.sh` script.

For my build I edited the `build.sh` file and changed the location of the build directory *cmake* to go at the top 
of the *azure-iot-sdks* path, instead of my home folder.

You need to build the library with the __-fPIC__ option so the build command is:

	build.sh -cl -fPIC

Once the Azure iot sdk library is built you can then go to this folder and run `make`.

For the build I assume that the *azure-iot-sdks* folder is at the same level as this folder, if not you can set the 
makefile environment variables to the location of the azure sdk source tree and azure sdk built libraries.

See the enviroment variables in the `Makefile`:

	AZURE_IOTHUB_INC_DIR ?= ../azure-iot-sdks/c
	AZURE_IOTHUB_LIB_DIR ?= ../azure-iot-sdks/cmake

## Yocto Build

I have also built the Azure iot sdk library and the lua library as two seperate using yocto recipes.

[Azure Yocto Static Library Recipe](https://github.com/billbsing/meta-silverline/blob/fido/meta-core/recipes-devtools/azure-iot-hub-lib/azure-iot-hub-lib_git.bb)

[Lua Azure Iot Hub Recipe](https://github.com/billbsing/meta-silverline/blob/fido/meta-core/recipes-luarocks/lua-azure-iot-hub/lua-azure-iot-hub_git.bb)

