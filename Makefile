# Default prefix
PREFIX ?= /usr

# System's libraries directory (where binary libraries are installed)
LUA_LIBDIR ?= $(PREFIX)/lib/lua/5.2

# System's lua directory (where Lua libraries are installed)
LUA_DIR ?= $(PREFIX)/share/lua/5.2

# Lua includes directory
LUA_INC ?= $(PREFIX)/include/lua5.2

AZURE_IOTHUB_INC = ../azure-iot-sdks
AZURE_IOTHUB_LIB = ../azure-iot-sdks/cmake

LIBPATH ?= /usr/lib
INCLUDEPATH ?= /usr/include


INSTALL ?= install


# support cross compile options
CC  := cc
CXX := g++
LD  := g++



# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CFLAGS  := -Wall  -fPIC
#INCLUDES := -I$(INCLUDEPATH)/glib-2.0 -I$(LIBPATH)/glib-2.0/include -I$(INCLUDEPATH) -I$(LUA_INC)
INCLUDES := -I$(INCLUDEPATH) -I$(LUA_INC) -Isrc -I$(AZURE_IOTHUB_INC)/c/iothub_client/inc -I$(AZURE_IOTHUB_INC)/c/azure-c-shared-utility/c/inc

# LFLAGS := -shared -L$(AZURE_IOTHUB_LIB)/iothub_client
LFLAGS := -L$(LUA_LIBDIR) -L$(AZURE_IOTHUB_LIB)/iothub_client -L$(AZURE_IOTHUB_LIB)/azure-c-shared-utility/c -L/usr/lib -L$(AZURE_IOTHUB_LIB)/azure-uamqp-c -L$(AZURE_IOTHUB_LIB)/azure-umqtt-c

CORE_LIBS := -luuid
SSL_LIBS := -lssl -lcrypto
CURL_LIBS := -lcurl
AZURE_LIBS := -liothub_client -liothub_client_http_transport -liothub_client_amqp_transport -liothub_client_mqtt_transport -laziotsharedutil -luamqp -lumqtt

# LIBS := -lglib-2.0
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
	$(INSTALL) -m 0644 $(TARGET) $(LUA_LIBDIR)/$(TARGET)
	

.PHONY:	all clean install
