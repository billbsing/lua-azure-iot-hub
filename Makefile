# Default prefix
PREFIX ?= /usr

# System's libraries directory (where binary libraries are installed)
LUA_LIB_DIR ?= $(PREFIX)/lib/lua/5.2

# System's lua directory (where Lua libraries are installed)
LUA_DIR ?= $(PREFIX)/share/lua/5.2

# Lua includes directory
LUA_INC ?= $(PREFIX)/include/lua5.2

AZURE_IOTHUB_INC_DIR ?= ../azure-iot-sdks/c
AZURE_IOTHUB_LIB_DIR ?= ../azure-iot-sdks/cmake

LIB_DIR ?= /usr/lib
INCLUDE_DIR ?= /usr/include


INSTALL ?= install


# support cross compile options
CC  := cc
CXX := g++
LD  := g++



# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CFLAGS  := -Wall  -fPIC
AZURE_INCLUDES := -I$(AZURE_IOTHUB_INC_DIR)/iothub_client/inc -I$(AZURE_IOTHUB_INC_DIR)/azure-c-shared-utility/c/inc
INCLUDES := -I$(INCLUDE_DIR) -I$(LUA_INC) -Isrc $(AZURE_INCLUDES)

AZURE_LIBS := -L$(AZURE_IOTHUB_LIB_DIR)/iothub_client -L$(AZURE_IOTHUB_LIB_DIR)/azure-c-shared-utility/c -L$(AZURE_IOTHUB_LIB_DIR)/azure-uamqp-c -L$(AZURE_IOTHUB_LIB_DIR)/azure-umqtt-c
LFLAGS :=  -L$(LIB_DIR) -L$(LUA_LIB_DIR) $(AZURE_LIBS)

CORE_LIBS := -luuid
SSL_LIBS := -lssl -lcrypto
CURL_LIBS := -lcurl
AZURE_LIBS := -liothub_client -liothub_client_http_transport -liothub_client_amqp_transport -liothub_client_mqtt_transport -laziotsharedutil -luamqp -lumqtt

LIBS :=  -shared $(CORE_LIBS) $(SSL_LIBS) $(CURL_LIBS) $(AZURE_LIBS)


# the build target library:
TARGET = luaazureiothub.so

SOURCES = src/luaazureiothub.c
OBJECTS = $(SOURCES:.c=.o)


all:    $(TARGET)
	@echo  $(TARGET) has been built

$(TARGET): $(OBJECTS) 
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET) $(OBJECTS) $(LFLAGS) $(LIBS)
#	ar rc $(TARGET) $(OBJECTS)
	
.c.o: $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -c $<  -o $@

clean:
	$(RM) *.o *~ $(TARGET)


install: $(TARGET)
	$(INSTALL) -d $(LUA_LIBDIR)
	$(INSTALL) -m 0644 $(TARGET) $(LUA_LIB_DIR)/$(TARGET)
	

.PHONY:	all clean install
