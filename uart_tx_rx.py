from curses import start_color
import serial
import numpy as np
import time

# True for debugging the code
debug = False

# Declaration of 1byte Start, End and Acknowledge
start_byte = np.uint8(0x8D)
end_byte   = np.uint8(0x8F)
ack_byte   = 0x90

# CRC Algorithm that calculates the CRC
# param1: Input buffer in np.array
# param2: Start Position of data in the buffer
# param3: Length of data
# param4: CRC Generator polynomial value
# return: CRC value in np.uint8
def generate_crc(data, position, length, crc_generator):
	
	# Initialize CRC byte
	crc = np.uint8(0x00)

	# Main CRC Algorithm
	for i in range(position,position+length):
		crc ^= np.uint8(data[i])
		for j in range(0,8):
			if crc & np.uint8(0x80) !=0:
				crc = np.uint8((crc<<1))^np.uint8(crc_generator)
			else:
				crc<<=np.uint8(1)

	# Return the calculated CRC value				
	return crc

# Initialize and check if COM Port exists
while True:

	# User input for COM Port
	comport = input("Enter COM Port : ")
	
	try:
		serial_line = serial.Serial(
			# Serial Port to read the data from
			port=comport,

			#Rate at which the information is shared to the communication channel
			baudrate = 2400,
		  
			#Applying Parity Checking (none in this case)
			parity=serial.PARITY_NONE,

		       # Pattern of Bits to be read
			stopbits=serial.STOPBITS_ONE,
		    
			# Total number of bits to be read
			bytesize=serial.EIGHTBITS,

			# Read timeout
			timeout=1
		)
		
		# Clear the input and output buffer
		serial_line.flushOutput()
		serial_line.flushInput()
		break

	except:
		print("Invalid COM Port\n")

# Data to be sent		
data = "Finance Minister Arun Jaitley Tuesday hit out at former RBI governor Raghuram Rajan for predicting that the next banking crisis would be triggered by MSME lending, saying postmortem is easier than taking action when it was required. Rajan, who had as the chief economist at IMF warned of impending financial crisis of 2008, in a note to a parliamentary committee warned against ambitious credit targets and loan waivers, saying that they could be the sources of next banking crisis. Government should focus on sources of the next crisis, not just the last one. In particular, government should refrain from setting ambitious credit targets or waiving loans. Credit targets are sometimes achieved by abandoning appropriate due diligence, creating the environment for future NPAs,\" Rajan said in the note.\" Both MUDRA loans as well as the Kisan Credit Card, while popular, have to be examined more closely for potential credit risk. Rajan, who was RBI governor for three years till September 2016, is currently."
# Converting the data into numpy array
numpy_data_array = np.array(data,'c').view(np.uint8)

# Current position to keep a track of which data is to be sent next
current_position = 0

# Send the data until current position does not become greater than lenght of data
while current_position <= len(numpy_data_array):
	
	# If difference between current value and length of data is greater than or equal to 4 then send 4bytes of data
	if(len(numpy_data_array)-current_position>=4):
		crc_value = generate_crc(numpy_data_array,current_position,4,0x31)
		data_len = 4
	# Else send no of bytes remaining
	else:
		crc_value = generate_crc(numpy_data_array,current_position,len(numpy_data_array)-current_position,0x31)
		data_len = len(numpy_data_array)-current_position

	# For debugging purposes
	if debug:
		print(hex(data_len),[chr(data) for data in numpy_data_array[current_position:current_position+data_len]],hex(crc_value),current_position,len(numpy_data_array))

    # Create a list of all the fields to be sent START BYTE + DATA LENGTH + DATA[0] +...+ DATA[DATA_LEGTH] + CRC BYTE + END BYTE
	send_list = list()
	send_list.append(start_byte)
	send_list.append(data_len)
	for each_data in numpy_data_array[current_position:current_position+data_len]:
		send_list.append(each_data)
	send_list.append(crc_value)
	send_list.append(end_byte)

	# For debugging purposes
	if debug:
		print(bytearray(send_list))

	start_time = time.time()
	# Send the list of fields slong with data on serial line
	serial_line.write(bytearray(send_list))
	end_time = time.time()
	print("Transmission Rate : ",len(send_list)*8/(end_time-start_time),"bits/second")

	# Wait for ackowledge byte until timeout occured
	if(int.from_bytes(serial_line.read(1), "big") == ack_byte):
		# Update the current position to send next set of data
		current_position = current_position+4

# Time before we start
start_time = time.time()
# Total time passed
total_time = 0

# Send 0xFF to start receiving data from microcontrollers side
serial_line.write(b'\xFF')

while True:

	# Read the current character
	current_char = serial_line.read(1)
	# If current character is not empty 
	if current_char != b'':
		# Time when next byte is received
		end_time = time.time()
		
		total_time = total_time+end_time-start_time
		
		if debug:
			print(total_time)

		print(current_char,"\t\t Reception Rate : " ,8/(end_time-start_time),"bits/second")

		# Reset the start time
		start_time = end_time