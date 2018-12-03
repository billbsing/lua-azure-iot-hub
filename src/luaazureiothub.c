/***
## Azure Iot Hub Device SDK for Lua

This library provides the lua interface to the Microsoft Azure IotHub SDK.


See the [Microsoft Azure IoT Device SDK for C documentation](http://azure.github.io/azure-iot-sdks/c/api_reference/index.html) which this library uses to
access the Azure Iot Hub.

@module luaazureiothub


*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

#include "xio.h"
#include "tlsio_openssl.h"
#include "iothub_client.h"
#include "iothub_message.h"
#include "threadapi.h"
#include "crt_abstractions.h"
#include "iothubtransportamqp.h"
#include "iothubtransporthttp.h"
#include "iothubtransportmqtt.h"

#include "luaazureiothub.h"


#define SEND_TIMEOUT_SECONDS						240
#define RECEIVE_FUNCTION_CALLBACK_NAME				"luaazureiothub_receive_function"
#define SEND_CONFIRMATION_FUNCTION_CALLBACK_NAME	"luaazureiothub_send_confirmation_function"

DEFINE_ENUM_STRINGS(IOTHUB_CLIENT_CONFIRMATION_RESULT, IOTHUB_CLIENT_CONFIRMATION_RESULT_VALUES);

#define LIBRARY_VERSION  	"1.1.0"

typedef struct {
	IOTHUB_CLIENT_LL_HANDLE iotHubClientHandle;
	bool isConnected;
} ConnectInfo;




typedef struct {
	IOTHUB_CLIENT_CONFIRMATION_RESULT result;
	bool isDone;
} SyncSendStatus;

typedef struct {
	IOTHUB_MESSAGE_HANDLE messageHandle;
	IOTHUB_CLIENT_CONFIRMATION_RESULT result;
	char *messageId;
	bool isSendSync;
} SendCallbackInfo;


static lua_State *callbackState;
static SyncSendStatus syncSendStatus;

static int luaLibInfo(lua_State *L);
static int luaConnect(lua_State *L);
static int luaGenerateUUID(lua_State *L);

static luaL_Reg luaAzureIotHubMethods[] = {
	{"info", luaLibInfo },
	{"connect", luaConnect },
	{"generateUUID", luaGenerateUUID },
	{NULL, NULL}
};

static int luaDisconnect(lua_State *L);
static int luaSendMessage(lua_State *L);
static int luaGetSendStatus(lua_State *L);
static int luaLastMessageReceiveTime(lua_State *L);
static int luaLoop(lua_State *L);


static luaL_Reg luaAzureIotHubConnectionMethods[] = {
	{"disconnect", luaDisconnect },
	{"sendMessage", luaSendMessage },
	{"getSendStatus", luaGetSendStatus },
	{"lastMessageReceiveTime", luaLastMessageReceiveTime },
	{"loop", luaLoop },
	{NULL, NULL}
};


void pushConnectInfo(lua_State *L, ConnectInfo *info)
{
	lua_pushstring(L, "info");
	ConnectInfo *userData = lua_newuserdata(L, sizeof(ConnectInfo));
	memcpy(userData, info, sizeof(ConnectInfo));
	lua_settable(L, -3);
}

ConnectInfo *readConnectInfo(lua_State *L, int index)
{
	ConnectInfo *info = NULL;
	if ( lua_istable(L, index) ) {
		lua_pushstring(L, "info");
		lua_gettable(L, index);
		info = lua_touserdata(L, -1);
	}
	return info;
}


void pushMessageTable(lua_State *L, IOTHUB_MESSAGE_HANDLE messageHandle)
{
    IOTHUBMESSAGE_CONTENT_TYPE contentType = IoTHubMessage_GetContentType(messageHandle);

	lua_createtable(L, 2, 2);

	lua_pushstring(L, "contentType");
	lua_pushnumber(L, contentType);
	lua_settable(L, -3);

    if ( ! ( contentType == IOTHUBMESSAGE_BYTEARRAY || contentType == IOTHUBMESSAGE_STRING ) ) {
		lua_pushstring(L, "errorMessage");
		lua_pushstring(L, "invalid message content");
		lua_settable(L, -3);
		return;
	}

    if (contentType == IOTHUBMESSAGE_BYTEARRAY) {
    	const unsigned char *buffer = NULL;
    	size_t size = 0;
        if (IoTHubMessage_GetByteArray(messageHandle, &buffer, &size) == IOTHUB_MESSAGE_OK) {
			lua_pushstring(L, "text");
			lua_pushlstring(L, (const char *) buffer, size);
			lua_settable(L, -3);
			lua_pushstring(L, "length");
			lua_pushnumber(L, size);
			lua_settable(L, -3);
		}
		else {
			lua_pushstring(L, "errorMessage");
			lua_pushstring(L, "cannot save data");
			lua_settable(L, -3);
		}
	}
    if (contentType == IOTHUBMESSAGE_STRING) {
    	const char *buffer;
        if ( (buffer = IoTHubMessage_GetString(messageHandle)) != NULL ) {
			lua_pushstring(L, "text");
			lua_pushstring(L, buffer);
			lua_settable(L, -3);
		}
		else {
			lua_pushstring(L, "errorMessage");
			lua_pushstring(L, "cannot save text");
			lua_settable(L, -3);
		}
	}


	lua_pushstring(L, "id");
	lua_pushstring(L, IoTHubMessage_GetMessageId(messageHandle));
	lua_settable(L, -3);

	lua_pushstring(L, "correlationId");
	lua_pushstring(L, IoTHubMessage_GetCorrelationId(messageHandle));
	lua_settable(L, -3);

    // Retrieve properties from the message
    MAP_HANDLE mapProperties = IoTHubMessage_Properties(messageHandle);
    if (mapProperties != NULL) {
        const char*const* keys;
        const char*const* values;
        size_t propertyCount = 0;
        if (Map_GetInternals(mapProperties, &keys, &values, &propertyCount) == MAP_OK) {
            if (propertyCount > 0) {
				lua_pushstring(L, "property");
				lua_createtable(L, 0, propertyCount);
                size_t index;
                for (index = 0; index < propertyCount; index++) {
					lua_pushstring(L, keys[index]);
					lua_pushstring(L, values[index]);
					lua_settable(L, -3);
                }
                lua_settable(L, -3);
            }
        }
    }
}

static IOTHUBMESSAGE_DISPOSITION_RESULT ReceiveMessageCallback(IOTHUB_MESSAGE_HANDLE messageHandle, void* userContextCallback)
{
	lua_State *L = callbackState;
	IOTHUBMESSAGE_DISPOSITION_RESULT result = IOTHUBMESSAGE_ACCEPTED;
	lua_getfield(L, LUA_REGISTRYINDEX, RECEIVE_FUNCTION_CALLBACK_NAME);
	if ( L && lua_isfunction(L, -1) ) {
		pushMessageTable(L, messageHandle);
		lua_call(L, 1, 1);
		if ( lua_isnumber(L, -1) ) {
			result = lua_tonumber(L, -1);
		}
		lua_pop(L, 1);
	}
	else {
		lua_pop(L, 1);			// get receiveFunction
	}
    return result;
}

static void SendConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* userContextCallback)
{
	IOTHUBMESSAGE_CONTENT_TYPE contentType;
	SendCallbackInfo *sendCallbackInfo = ( SendCallbackInfo *) userContextCallback;
	lua_State *L = callbackState;
	if ( sendCallbackInfo == NULL || L == NULL ) {
		return;
	}
	// check for bug when sending error, the service can send multiple callbacks on one message
    contentType = IoTHubMessage_GetContentType(sendCallbackInfo->messageHandle);

    if ( contentType != IOTHUBMESSAGE_BYTEARRAY && contentType != IOTHUBMESSAGE_STRING ) {
		return;
	}

	lua_getfield(L, LUA_REGISTRYINDEX, SEND_CONFIRMATION_FUNCTION_CALLBACK_NAME);
	if ( lua_isfunction(L, -1) ) {
		lua_pushnumber(L, result);
		pushMessageTable(L, sendCallbackInfo->messageHandle);
		lua_call(L, 2, 0);
	}
	lua_pop(L, 1); 			// pop back table getfield sendConfirmFunction

	// check to see if we saved a random uuid for this message
	if ( result != IOTHUB_CLIENT_CONFIRMATION_ERROR) {
		if ( sendCallbackInfo->isSendSync ) {
			syncSendStatus.isDone = true;
			syncSendStatus.result = result;
		}
		IoTHubMessage_Destroy(sendCallbackInfo->messageHandle);
	}
	if ( sendCallbackInfo->messageId ) {
		free(sendCallbackInfo->messageId);
		sendCallbackInfo->messageId = NULL;
	}
	free(sendCallbackInfo);
}

/***
IotHub object, returned by the @{connect} function
@table iotHub
@tfield boolean isConnect If true then this connection has been made
@tfield function disconnect @{disconnect} Disconnects from the IotHub.
@tfield function sendMessage @{sendMessage} Sends out a message.
@tfield function getSendStatus @{getSendStatus} Returns the current sending status.
@tfield function lastMessageReceiveTime @{lastMessageReceiveTime} Returns the last time a message was received.
@tfield function loop @{loop} Loops around the message queue completing sending and receiving messages.
*/


/***
Message table used for sending or receiving via the Azure IotHub.

On sending a message, you need to only fill in the __text__ field before passing this table to the @{sendMessage} function.

On receving a message the __text__, __contentType__ and __length__ will have values, other fields will be set if they are present in the message.

@table message
@tfield string text Mesasge text, this can be a text string or binary data with embedded zeros.
@tfield number,nil length If the length is provided then the text field will be encoded as binary data, with that length
@tfield number,nil contentType The message text can be overridden by using this field. It can be one of the following:


	nil                   Use the length field too decide which encoding too use string or byte.
	contentType.BYTE      Always encode using byte encoding.
	contentType.STRING    Always encode using string encoding.

@tfield string,nil id Message id, if set to nil, then the @{sendMessage} function will automatically assign a random uuid
@tfield string,nil correlationId You can read/write the correlationId.
@tfield table,nil property Set of name="Value" pairs as property values to send with the message.

@usage
-- basic text message
local message = {
  text = 'Basic text message'
}
-- sends out the message
iothub:sendMessage(message)
-- also sends out the same message above
iothub:sendMessage('Basic text message')

-- basic data message with a fixed length of 12
local message = {
  text = 'Basic data message',
  length = 12
}

-- forced text message to always send out 12 character string
local message = {
  text = 'Basic data message',
  length = 12,
  contentType = luaazureiothub.contentType.STRING,
}

-- string message with a property value of 'messageType', and a special message id
local message = {
  text = 'Basic text message',
  property = {
     messageType='event',
  },
  id = 'My test id',
}

*/


/***
Connect to the Azure IotHub, if successfull returns an IotHub object.
@function connect
@tparam string connectString String to connect to the Azure IotHub
@tparam[opt=AMQP] string protocol Name of the protocol (case insensitive), can be 'AMQP', 'MQTT' or 'HTTP'
@tparam[opt=nil] function processRead Function to process read messages, see the callback function @{processRead}.
@tparam[opt=nil] function processSent Function to process reply after sending a message, see the callback function @{processSent}.
@treturn iotHub object table if successfully connected to the IotHub.
@treturn false, errorMessage False and an error message if failed to connect

@usage
local luaazureiothub = require 'luaazureiothub'

local processRead = function(message)
  print("received a message:")
  print(message)
  return luaazureiothub.messageReceive.ACCEPTED
end

local processSendConfirmation = function(status, message)
  -- called when the message has been sent or has an error
  if status == luaazureiothub.messageSend.OK then
    print('RX message ack')
  else
    print('RX message error:' .. status)
  end
  print(message)
end

local connectionString = 'HostName=hostname.azure-devices.net;DeviceId=deviceId;SharedAccessKey=????'
local iothub, errorMessage = luaazureiothub.connect(connectionString, 'amqp', processRead, processSendConfirmation)

*/

static int luaConnect(lua_State *L)
{

	const char *connectionString = NULL;
	IOTHUB_CLIENT_TRANSPORT_PROVIDER protocol = NULL;
	ConnectInfo info;

// Lua call params
// connect( connectionString, [protocol = AMQP, receiveCoRoutine] )


	if ( !lua_isstring(L, 1) ) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "Parameter #1 is not a connection string");
		return 2;
	}

	connectionString = lua_tostring(L, 1);


	if ( lua_isstring(L, 2) ) {
		protocol = NULL;
		const char *protocolText = lua_tostring(L, 2);
		if ( strcasecmp("AMQP", protocolText) == 0 ) {
			protocol = AMQP_Protocol;
		}
		if ( strcasecmp("HTTP", protocolText) == 0 ) {
			protocol = HTTP_Protocol;
		}
		if ( strcasecmp("MQTT", protocolText) == 0 ) {
			protocol = MQTT_Protocol;
		}
		if ( protocol == NULL ) {
			lua_pushboolean(L, 0);
			lua_pushstring(L, "Parameter #2 can only be 'amqp', 'http' or 'mqtt'");
			return 2;
		}
	}
	else {
		protocol = AMQP_Protocol;
	}


	info.iotHubClientHandle = IoTHubClient_LL_CreateFromConnectionString(connectionString, protocol);
	if ( info.iotHubClientHandle == NULL ) {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "Failed to connect");
		return 2;
	}


	callbackState = L;
//	lua_pushthread(callbackState);

//	lua_createtable(callbackState, 0, 2);

	if ( lua_isfunction(L, 3) ) {
		lua_pushstring(callbackState, RECEIVE_FUNCTION_CALLBACK_NAME);
		lua_pushvalue(L, 3);
		lua_xmove(L, callbackState, 1);
		lua_settable(callbackState, LUA_REGISTRYINDEX );
	}
	if ( lua_isfunction(L, 4) ) {
		lua_pushstring(callbackState, SEND_CONFIRMATION_FUNCTION_CALLBACK_NAME);
		lua_pushvalue(L, 4);
		lua_xmove(L, callbackState, 1);
		lua_settable(callbackState, LUA_REGISTRYINDEX);
	}

	if (IoTHubClient_LL_SetMessageCallback(info.iotHubClientHandle, ReceiveMessageCallback, NULL) != IOTHUB_CLIENT_OK) {
		IoTHubClient_LL_Destroy(info.iotHubClientHandle);
		lua_pushboolean(L, 0);
		lua_pushstring(L, "Cannot setup message callback");
		return 2;
	}
	tlsio_openssl_init();
	luaL_newlib(L, luaAzureIotHubConnectionMethods);
	info.isConnected = true;
	pushConnectInfo(L, &info);
	lua_pushstring(L, "isConnect");
	lua_pushboolean(L, 1);
	lua_settable(L, -3);

	return 1;
}


/***
Return a random uuid.
@function generateUUID
@treturn string random uuid

*/
static int luaGenerateUUID(lua_State *L)
{
	uuid_t uuid;
	uuid_generate(uuid);
	char buffer[40];
	uuid_unparse(uuid, buffer);
	lua_pushstring(L, buffer);
	return 1;
}

/***
Return the current library version.
@function info
@treturn string Library version string

*/
static int luaLibInfo (lua_State *L)
{
	lua_pushstring(L, "luaazureiothub version 0.1");
	return 1;
}

/***
Callback Functions.
These functions are called from the library to send back data and return status of sent messages from the Iot Hub.
@section Callbacks

*/


/***
Callback function to read messages sent from the IotHub.
You need to create a function with these parameters and pass the function as a parameter to the @{connect} function.
@function processRead
@tparam message message The @{message} table sent from the IotHub.
@treturn integer You can return values from the static table @{messageReceive}, to accept or reject the message.
If you do not return a valid integer then the library will return messageReceive.ACCEPTED as default.

*/


/***
Callback function to accept a sent message.
You need to create a function with these parameters and pass the function as a parameter to the @{connect} function.
@function processSent
@tparam integer status The status of the sent message, see the static table @{messageSend} for the possible values.
@tparam message message A copy of the @{message} that has been sent. This message has been re-encoded from the C library
so it will not have any extra fields added when used in the @{sendMessage} function.

*/

/***
IotHub Class.
This class is returned by the @{connect} function.
@section IotHub

*/

/***
Disconnect from the iothub
@function iotHub:disconnect
@treturn boolean True if succesfull in disconecting

*/
static int luaDisconnect(lua_State *L)
{
	ConnectInfo *info = readConnectInfo(L, 1);
	if ( info ) {
		if ( info->iotHubClientHandle && info->isConnected ) {
			IoTHubClient_LL_Destroy(info->iotHubClientHandle);
			info->isConnected = false;
			info->iotHubClientHandle = NULL;
			tlsio_openssl_deinit();
		}
		lua_getfield(L, 1, "isConnect");
		if ( lua_isboolean(L, -1) ) {
			lua_pushvalue(L, 1);
			lua_pushstring(L, "isConnect");
			lua_pushboolean(L, 0);
			lua_settable(L, -3);
		}
		lua_pop(L, 2);			// isConnect field, info user data
	}
    if ( callbackState) {
        lua_close(callbackState);
        callbackState = NULL;
    }

	lua_pushboolean(L, 1);
	return 1;
}


/***
Sends a message to the Azure IotHub
@function iotHub:sendMessage
@tparam table,string message Mesasge to send, this field can be a string or a @{message}  table. If you use a string then
message sent will be a simple message with string encoding.
@tparam[opt=5] number timeoutSeconds Number of seconds to wait for the Ack reply to be recieved from the IotHub

if the timeoutSeconds == 0, then this function will return as soon as the message has been sent. It is up to the calling
code to then call the @{loop} function to wait for the message ack to be sent back from the IotHub.

@treturn boolean,integer True if successfully sent the message, and the result code returned from the send confirmation.
see the static @{messageSend} table of possible values.
@treturn boolean,string,integer False with the error message, and extra error code returned from the call to send message.
See the static values in the table @{clientResult} for the error codes returned.

*/

static int luaSendMessage(lua_State *L)
{
	SendCallbackInfo *sendCallbackInfo;
	ConnectInfo *info = readConnectInfo(L, 1);
	IOTHUBMESSAGE_CONTENT_TYPE contentType = IOTHUBMESSAGE_STRING;
	int timeoutSeconds = SEND_TIMEOUT_SECONDS;
	const char *messageText;
	int messageTextLength = 0;
	bool isMessageValid = false;
	IOTHUB_CLIENT_STATUS sendStatus;



	if ( info && info->iotHubClientHandle && info->isConnected ) {

		// check to see if the first param is a string
		if ( lua_isstring(L, 2) ) {
			messageText = lua_tostring(L, 2);
			contentType = IOTHUBMESSAGE_STRING;
			isMessageValid = true;
		}

		// check to see if the first param is a message table
		if ( lua_istable(L, 2) ) {

			// message.text
			lua_getfield(L, 2, "text");
			if ( !lua_isstring(L, -1) ) {
				lua_pushboolean(L, 0);
				lua_pushstring(L, "message.text must be used");
				return 2;
			}
			messageText = lua_tostring(L, -1);
			lua_pop(L, 1);			// remove text field

			// message.length
			lua_getfield(L, 2, "length");
			if ( lua_isnumber(L, -1) ) {
				contentType = IOTHUBMESSAGE_BYTEARRAY;
				messageTextLength = lua_tonumber(L, -1);
			}
			lua_pop(L, 1);			// remove length field
			messageTextLength = strlen(messageText);


			// message.contentType
			lua_getfield(L, 2, "contentType");
			if ( lua_isnumber(L, -1) ) {
				contentType = lua_tointeger(L, -1);
				if ( ! ( contentType == IOTHUBMESSAGE_BYTEARRAY || contentType == IOTHUBMESSAGE_STRING ) ) {
					contentType = IOTHUBMESSAGE_BYTEARRAY;
				}
			}
			lua_pop(L, 1);		// remove contentType field
			isMessageValid = true;

		}
		if ( ! isMessageValid) {
			lua_pushboolean(L, 0);
			lua_pushstring(L, "Parameter #2 must be a string or table");
			return 2;
		}

		sendCallbackInfo = (SendCallbackInfo *) malloc(sizeof(SendCallbackInfo));
		sendCallbackInfo->messageHandle = NULL;
		sendCallbackInfo->messageId = NULL;
		sendCallbackInfo->isSendSync = false;


		if ( contentType == IOTHUBMESSAGE_BYTEARRAY ) {
			sendCallbackInfo->messageHandle = IoTHubMessage_CreateFromByteArray((const unsigned char *) messageText, messageTextLength);
			if ( sendCallbackInfo->messageHandle == NULL ) {
				free(sendCallbackInfo);
				lua_pushboolean(L, 0);
				lua_pushstring(L, "Cannot create binary message");
				return 2;
			}
		}
		if ( contentType == IOTHUBMESSAGE_STRING ) {
			sendCallbackInfo->messageHandle = IoTHubMessage_CreateFromString(messageText);
			if ( sendCallbackInfo->messageHandle == NULL ) {
				free(sendCallbackInfo);
				lua_pushboolean(L, 0);
				lua_pushstring(L, "Cannot create string message");
				return 2;
			}
		}

		// last check to see if we have a message
		if ( sendCallbackInfo->messageHandle == NULL ) {
			free(sendCallbackInfo);
			lua_pushboolean(L, 0);
			lua_pushstring(L, "Cannot create message");
			return 2;
		}


		// check to see if the first param is a message table
		if ( lua_istable(L, 2) ) {
			// now set the message using with the messageHandle
			// message.id

			lua_getfield(L, 2, "id");
			if ( lua_isstring(L, -1) ) {
				IoTHubMessage_SetMessageId(sendCallbackInfo->messageHandle, lua_tostring(L, -1) );
			}
			else {
				uuid_t uuid;
				uuid_generate(uuid);
				char buffer[40];
				uuid_unparse(uuid, buffer);
				IoTHubMessage_SetMessageId(sendCallbackInfo->messageHandle, buffer);
			}
			lua_pop(L, 1);			// remove id field

			// correlationId
			lua_getfield(L, 2, "correlationId");
			if ( lua_isstring(L, -1) ) {
				IoTHubMessage_SetCorrelationId(sendCallbackInfo->messageHandle, lua_tostring(L, -1));
			}
			lua_pop(L, 1);			// remove correlationId field


			// property
			lua_getfield(L, 2, "property");
			if ( lua_istable(L, -1) ) {
				MAP_HANDLE propertyMap = IoTHubMessage_Properties(sendCallbackInfo->messageHandle);
				lua_pushnil(L);  // first key
				while (lua_next(L, -2) != 0) {
					// uses 'key' (at index -2) and 'value' (at index -1)
					const char *propertyName;
					const char *propertyValue;
					propertyName = lua_tostring(L, -2);
					propertyValue = lua_tostring(L, -1);
					if (Map_AddOrUpdate(propertyMap, propertyName, propertyValue) != MAP_OK) {
						IoTHubMessage_Destroy(sendCallbackInfo->messageHandle);
						free(sendCallbackInfo);
						lua_pushboolean(L, 0);
						lua_pushfstring(L, "Cannot assign message property %s=%s", propertyName, propertyValue);
						return 2;
					}
					// removes 'value'; keeps 'key' for next iteration
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1); 		// remove property field
		}

		// look for param #3 , timeout seconds
		if ( lua_isnumber(L, 3) ) {
			timeoutSeconds = lua_tointeger(L, 3);
		}


		if ( IoTHubClient_LL_GetSendStatus(info->iotHubClientHandle, &sendStatus) ==  IOTHUB_CLIENT_OK ) {
			if (sendStatus != IOTHUB_CLIENT_SEND_STATUS_IDLE ) {
			    free(sendCallbackInfo);
				lua_pushboolean(L, 0);
				lua_pushstring(L, "Busy");
				return 2;
			}
		}
		else {
		    free(sendCallbackInfo);
			lua_pushboolean(L, 0);
			lua_pushstring(L, "Unable to get send status");
			return 2;
		}

		sendCallbackInfo->isSendSync = (timeoutSeconds != 0);
		syncSendStatus.isDone = false;
		const char *messageId = IoTHubMessage_GetMessageId(sendCallbackInfo->messageHandle );

		if ( messageId ) {
			sendCallbackInfo->messageId = strdup(messageId);
		}

		// send the message
		IOTHUB_CLIENT_RESULT result = IoTHubClient_LL_SendEventAsync(info->iotHubClientHandle, sendCallbackInfo->messageHandle, SendConfirmationCallback, sendCallbackInfo);
		if ( result != IOTHUB_CLIENT_OK ) {
		    IoTHubMessage_Destroy(sendCallbackInfo->messageHandle);
		    free(sendCallbackInfo->messageId);
		    free(sendCallbackInfo);
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "Cannot send message %s", ENUM_TO_STRING(IOTHUB_CLIENT_RESULT, result));
			lua_pushnumber(L, result);
			return 3;
		}
		// return since we are in async mode
		if ( timeoutSeconds == 0 ) {
			lua_pushboolean(L, 1);
			return 1;
		}


		unsigned long timeout = time(NULL) + timeoutSeconds;
		while ( timeout > time(NULL)  ) {
			IoTHubClient_LL_DoWork(info->iotHubClientHandle);
			if ( IoTHubClient_LL_GetSendStatus(info->iotHubClientHandle, &sendStatus) ==  IOTHUB_CLIENT_OK ) {
				if (sendStatus == IOTHUB_CLIENT_SEND_STATUS_IDLE ) {
					if (  syncSendStatus.isDone ) {
						break;
					}
				}
			}
		}
		int returnStackSize = 0;
		if ( syncSendStatus.isDone ) {
			if ( syncSendStatus.result == IOTHUB_CLIENT_CONFIRMATION_OK ) {
				lua_pushboolean(L, 1);
				returnStackSize = 1;
			}
			else {
				lua_pushboolean(L, 0);
				lua_pushfstring(L, "Cannot send message, received: %s", ENUM_TO_STRING(IOTHUB_CLIENT_RESULT, sendCallbackInfo->result));
				lua_pushinteger(L, sendCallbackInfo->result);
				returnStackSize = 3;
			}
		}
		else {
			lua_pushboolean(L, 0);
			lua_pushstring(L, "timeout");
			returnStackSize = 2;
		}
		return returnStackSize;
	}
	else {
		lua_pushboolean(L, 0);
		lua_pushstring(L, "Not connected or IotHub object not found");
		return 2;
	}
	return 0;
}


/***
Loop around the event queue to process sending and receiving of messages.

You will need to call this if you whish to receive any messages and or if you have sent a message using @{sendMessage}
setting the timoutSeconds to 0.

@function iotHub:loop
@tparam[opt=1] integer timeoutSeconds Number of seconds to loop around and process the message queues. If you set this
value to <=0 then the loop will process only one cycle and return.

@usage
-- send out 10 async messages
for counter = 1, 10 do
  -- send message but do not wait for reply
  iotHub:sendMessage('Test message ' .. counter, 0)
  -- process the messages sent out and maybe also read some in
  iotHub:loop()
end

*/

static int luaLoop(lua_State *L)
{
	ConnectInfo *info = readConnectInfo(L, 1);

	// default to wait for one second
	int timeoutSeconds = 1;
	if ( lua_isnumber(L, 2) ) {
		timeoutSeconds = lua_tointeger(L, 2);
	}
	if ( info && info->iotHubClientHandle && info->isConnected ) {
		unsigned long timeout = time(NULL) + timeoutSeconds;
		IoTHubClient_LL_DoWork(info->iotHubClientHandle);
		while ( timeout > time(NULL) ) {
			IoTHubClient_LL_DoWork(info->iotHubClientHandle);
		}
	}
	return 0;
}

/***
Get the current send status of the send process
@function iotHub:getSendStatus
@treturn integer status of the current send process, see the @{sendStatus} table.
@treturn boolean,string False and the error message if the status cannot be retreived.

The returned status can be one of the following values:

  + sendStatus.IDLE
  + sendStatus.BUSY

*/

static int luaGetSendStatus(lua_State *L)
{
	ConnectInfo *info = readConnectInfo(L, 1);
	IOTHUB_CLIENT_STATUS status;

	if ( info && info->iotHubClientHandle && info->isConnected ) {
		if ( IoTHubClient_LL_GetSendStatus(info->iotHubClientHandle, &status) !=  IOTHUB_CLIENT_OK ) {
			lua_pushboolean(L, 0);
			lua_pushstring(L, "Unable to get send status");
			return 2;
		}
		lua_pushinteger(L, status);
		return 1;
	}
	lua_pushboolean(L, 0);
	lua_pushstring(L, "Not connected");
	return 2;
}

/***
Get the last time a message was received from the Azure IotHub
@function iotHub:lastMessageReceiveTime
@treturn number time in seconds as to the last time a message was received
@treturn boolean, string False and the error message if the time cannot be retreived
*/
static int luaLastMessageReceiveTime(lua_State *L)
{
	ConnectInfo *info = readConnectInfo(L, 1);
	time_t lastMessageReceiveTime;

	if ( info && info->iotHubClientHandle && info->isConnected ) {
		if ( IoTHubClient_LL_GetLastMessageReceiveTime(info->iotHubClientHandle, &lastMessageReceiveTime) !=  IOTHUB_CLIENT_OK ) {
			lua_pushboolean(L, 0);
			lua_pushstring(L, "Unable to get last message receive time");
			return 2;
		}
		lua_pushnumber(L, lastMessageReceiveTime);
		return 1;
	}
	lua_pushboolean(L, 0);
	lua_pushstring(L, "Not connected");
	return 2;
}



/***
Static values.
These tables contain static values that are returned or set by the Azure IotHub SDK.
@section static

*/


/***
Static values to pass back from the message recieve function
@table messageReceive
@tfield integer ACCEPTED returns the __IOTHUBMESSAGE\_ACCEPTED__ value
@tfield integer REJECTED returns the __IOTHUBMESSAGE\_REJECTED__ value
@tfield integer ABANDONED returns the __IOTHUBMESSAGE\_ABANDONED__ value
*/




/***
Static values to define the message content type in the @{message} table.
@table contentType
@tfield integer STRING returns the __IOTHUBMESSAGE\_STRING__ value
@tfield integer BYTE returns the __IOTHUBMESSAGE\_BYTEARRAY__ value
*/


/***
Static values that are sent from the IotHub after a message has been sent
@table messageSend
@tfield integer OK returns the __IOTHUB\_CLIENT\_CONFIRMATION\_OK__ value
@tfield integer DESTROY returns the __IOTHUB\_CLIENT\_CONFIRMATION\_BECAUSE\_DESTROY__ value
@tfield integer TIMEOUT returns the __IOTHUB\_CLIENT\_CONFIRMATION\_MESSAGE\_TIMEOUT__ value
@tfield integer ERROR returns the __IOTHUB\_CLIENT\_CONFIRMATION\_ERROR__ value
*/


/***
Static values to define the send status returned by the @{getSendStatus} function.
@table sendStatus
@tfield integer IDLE returns the __IOTHUB\_CLIENT\_SEND\_STATUS\_IDLE__ value
@tfield integer BUSY returns the __IOTHUB\_CLIENT\_SEND\_STATUS\_BUSY__ value
*/

/***
Static values to define the returned error code from the call to @{sendMessage} function.
@table clientResult
@tfield integer OK returns the __IOTHUB_CLIENT_OK__ value
@tfield integer INVALID_ARG returns the __IOTHUB_CLIENT_INVALID_ARG__ value
@tfield integer ERROR returns the __IOTHUB_CLIENT_ERROR__ value
@tfield integer INVALID_SIZE returns the __IOTHUB_CLIENT_INVALID_SIZE__ value
@tfield integer INDEFINITE_TIME returns the __IOTHUB_CLIENT_INDEFINITE_TIME__ value

*/

int luaopen_luaazureiothub (lua_State *L)
{
	luaL_newlib(L, luaAzureIotHubMethods);

	lua_pushstring(L, "messageReceive");
	lua_createtable(L, 0, 3);

	lua_pushstring(L, "ACCEPTED");
	lua_pushnumber(L, IOTHUBMESSAGE_ACCEPTED);
	lua_settable(L, -3);

	lua_pushstring(L, "REJECTED");
	lua_pushnumber(L, IOTHUBMESSAGE_REJECTED);
	lua_settable(L, -3);

	lua_pushstring(L, "ABANDONED");
	lua_pushnumber(L, IOTHUBMESSAGE_ABANDONED);
	lua_settable(L, -3);

	lua_settable(L, -3);	// messageReceive

	lua_pushstring(L, "contentType");
	lua_createtable(L, 0, 2);

	lua_pushstring(L, "STRING");
	lua_pushnumber(L, IOTHUBMESSAGE_STRING);
	lua_settable(L, -3);

	lua_pushstring(L, "BYTE");
	lua_pushnumber(L, IOTHUBMESSAGE_BYTEARRAY);
	lua_settable(L, -3);

	lua_settable(L, -3);		// contentType


	lua_pushstring(L, "messageSend");
	lua_createtable(L, 0, 2);

	lua_pushstring(L, "OK");
	lua_pushnumber(L, IOTHUB_CLIENT_CONFIRMATION_OK);
	lua_settable(L, -3);

	lua_pushstring(L, "DESTROYED");
	lua_pushnumber(L, IOTHUB_CLIENT_CONFIRMATION_BECAUSE_DESTROY);
	lua_settable(L, -3);

	lua_pushstring(L, "TIMEOUT");
	lua_pushnumber(L, IOTHUB_CLIENT_CONFIRMATION_MESSAGE_TIMEOUT);
	lua_settable(L, -3);

	lua_pushstring(L, "ERROR");
	lua_pushnumber(L, IOTHUB_CLIENT_CONFIRMATION_ERROR);
	lua_settable(L, -3);

	lua_settable(L, -3);		// messageSend


	lua_pushstring(L, "sendStatus");
	lua_createtable(L, 0, 2);

	lua_pushstring(L, "IDLE");
	lua_pushnumber(L, IOTHUB_CLIENT_SEND_STATUS_IDLE);
	lua_settable(L, -3);

	lua_pushstring(L, "BUSY");
	lua_pushnumber(L, IOTHUB_CLIENT_SEND_STATUS_BUSY);
	lua_settable(L, -3);

	lua_settable(L, -3);		// sendStatus


	lua_pushstring(L, "clientResult");
	lua_createtable(L, 0, 5);

	lua_pushstring(L, "OK");
	lua_pushnumber(L, IOTHUB_CLIENT_OK);
	lua_settable(L, -3);

	lua_pushstring(L, "INVALID_ARG");
	lua_pushnumber(L, IOTHUB_CLIENT_INVALID_ARG);
	lua_settable(L, -3);

	lua_pushstring(L, "ERROR");
	lua_pushnumber(L, IOTHUB_CLIENT_ERROR);
	lua_settable(L, -3);

	lua_pushstring(L, "INVALID_SIZE");
	lua_pushnumber(L, IOTHUB_CLIENT_INVALID_SIZE);
	lua_settable(L, -3);

	lua_pushstring(L, "INDEFINITE_TIME");
	lua_pushnumber(L, IOTHUB_CLIENT_INDEFINITE_TIME);
	lua_settable(L, -3);

	lua_settable(L, -3);		// clientResult

	lua_pushstring(L, LIBRARY_VERSION);
	lua_setfield(L, "version", -1);
	
	return 1;
}
