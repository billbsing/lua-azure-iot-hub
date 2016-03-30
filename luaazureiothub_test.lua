#!/usr/bin/env lua5.2


-- Test


print("Test luaazureiothub Library")

local std = require "std"
local posix = require 'posix'
local luaazureiothub  = require 'luaazureiothub'

print('Library Info :' .. luaazureiothub.info())

local connectionString

-- SG-04-testing1
-- UFROISyPnZ6YQOZqEc1C0O+vRMo5bleunEuruC+dqRI=
--connectionString = 'HostName=silverlineiothub.azure-devices.net;SharedAccessKeyName=iothubowner;SharedAccessKey=sTodn29a96qYG/KeoKfn/B7MVKc9oHDkT9kZrZiNDLw='
connectionString = 'HostName=silverlineiothub.azure-devices.net;DeviceId=SG-04-testing1;SharedAccessKey=UFROISyPnZ6YQOZqEc1C0O+vRMo5bleunEuruC+dqRI='


local maxSyncCounter = 0;

local processRead = function(message)
	local success, result = pcall(
		function()
			print("received a message:")
			print(message)
				
		--[[
			possible returns are..		
				luaazureiothub.messageReceive.ACCEPTED
				luaazureiothub.messageReceive.REJECTED
				luaazureiothub.messageReceive.ABANDONED
				
			if no return given then the default is ACCEPTED
		--]]		
			return luaazureiothub.messageReceive.ACCEPTED
		end
	)
    if success then
		return result
	else
        print(string.format('Error during scan %q', result))
    end
end

local processSendConfirmation = function(status, message)
	local success, result = pcall(
		function()
			-- called when the message has been sent or has an error 
			if status == luaazureiothub.messageSend.OK then
				print('RX message ack')
			else
				print('RX message error:' .. status)
			end
			print(message)
			if message.property then
				maxSyncCounter = message.property.syncCounter
			end
		end
	)
    if not success then
        print(string.format('Error during scan %q', result))
    end
end


local iothub, errorMessage = luaazureiothub.connect(connectionString, 'amqp', processRead, processSendConfirmation)


print('Result from connect:', iothub, errorMessage)
if iothub then

	local result = false
	local message = {
		text = 'Test text',
--		length = 2,   										-- optional: length if text is binary byte data
--		contentType = luaazureiothub.contentType.BYTE, 		-- optional: force the data to be byte or string
--		id = 'test_1234',									-- optional: message id, if left out then the system will generate 
															-- a random uuid instead
		correlationId = 'test1234',							-- optional: correlation id
		property = {										-- optional: property table for a list of property values
			propName = "propValue",
			testName = "testValue",
		},
	}
	
	print("send simple message string")
	assert(iothub:sendMessage('test message'))

	print("Test sync mode")
	for counter = 1, 10 do
		message.text = "Test message sync call " .. counter
		result, errorMessage = iothub:sendMessage(message )
		print('Result from send message:', counter,  result, errorMessage)
	end
	
	print("Test async mode")
	for counter = 1, 10 do
		message.text = "Test message async call " .. counter
		message.property.syncCounter = counter
		-- call the sendMesasge with a 0 as the timeoutSeconds, this sends the mesasge but does not wait for the result
		result, errorMessage = iothub:sendMessage(message, 0 )
		print('Result from send message:', counter,  result, errorMessage)
	end
	print("waiting for loop end until the 10th record comes back")
	
	while (tonumber(maxSyncCounter) == nil or tonumber(maxSyncCounter) < 10 ) do
		iothub:loop(2)
	end
	
	local timeout = posix.time() + 60
	while timeout > posix.time() do
		iothub:loop(2)
	end
	print('Result from sendStatus:', iothub:getSendStatus())
	print('Result from lastMessageReceiveTime:', iothub:lastMessageReceiveTime())
	iothub:disconnect()
end

print('Generate random uuid', luaazureiothub.generateUUID())


