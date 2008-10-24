#include "FalconCommLibUSB.h"
#include <iostream>
#include <cstdio>

//Taken from LibFTDI
//I just don't want to wait for the libusb 1.0 port of libftdi
//And I don't exactly want to do it myself
//So, here we are. File level defines.
#define SIO_RESET          0 /* Reset the port */
#define SIO_MODEM_CTRL     1 /* Set the modem control register */
#define SIO_SET_FLOW_CTRL  2 /* Set flow control register */
#define SIO_SET_BAUD_RATE  3 /* Set baud rate */
#define SIO_SET_DATA       4 /* Set the data characteristics of the port */

#define SIO_RESET_REQUEST_TYPE 0x40
#define SIO_RESET_REQUEST SIO_RESET
#define SIO_RESET_SIO 0
#define SIO_RESET_PURGE_RX 1
#define SIO_RESET_PURGE_TX 2

#define SIO_SET_BAUDRATE_REQUEST_TYPE 0x40
#define SIO_SET_BAUDRATE_REQUEST SIO_SET_BAUD_RATE

#define SIO_SET_DATA_REQUEST_TYPE 0x40
#define SIO_SET_DATA_REQUEST SIO_SET_DATA

#define SIO_SET_FLOW_CTRL_REQUEST SIO_SET_FLOW_CTRL
#define SIO_SET_FLOW_CTRL_REQUEST_TYPE 0x40

#define SIO_DISABLE_FLOW_CTRL 0x0 
#define SIO_RTS_CTS_HS (0x1 << 8)
#define SIO_DTR_DSR_HS (0x2 << 8)
#define SIO_XON_XOFF_HS (0x4 << 8)

#define SIO_SET_MODEM_CTRL_REQUEST_TYPE 0x40
#define SIO_SET_MODEM_CTRL_REQUEST SIO_MODEM_CTRL

#define SIO_SET_DTR_MASK 0x1
#define SIO_SET_DTR_HIGH ( 1 | ( SIO_SET_DTR_MASK  << 8))
#define SIO_SET_DTR_LOW  ( 0 | ( SIO_SET_DTR_MASK  << 8))
#define SIO_SET_RTS_MASK 0x2
#define SIO_SET_RTS_HIGH ( 2 | ( SIO_SET_RTS_MASK << 8 ))
#define SIO_SET_RTS_LOW ( 0 | ( SIO_SET_RTS_MASK << 8 ))

#define SIO_RTS_CTS_HS (0x1 << 8)

#define INTERFACE_ANY 0
#define INTERFACE_A 1
#define INTERFACE_B 2


namespace libnifalcon
{

	FalconCommLibUSB::FalconCommLibUSB()
	{
		m_tv.tv_sec = 0;
		m_tv.tv_usec = 100;
		m_requiresPoll = true;
		initLibUSB();
#if defined(LIBUSB_DEBUG)
		//Spam libusb messages
		//Between 0-3 for libusb 1.0
		libusb_set_debug(NULL, 3);
#endif

	}
	
	FalconCommLibUSB::~FalconCommLibUSB()
	{
		libusb_free_transfer(in_transfer);
		libusb_free_transfer(out_transfer);		
	}
	
	bool FalconCommLibUSB::initLibUSB()
	{
		int r = libusb_init(NULL);
		if (r < 0)
		{
			std::cout << "failed to initialise libusb" << std::endl;
			return false;
		}
		return true;
	}

	bool FalconCommLibUSB::getDeviceCount(int8_t& count)
	{
		count = 1;
		return true;
	}

	bool FalconCommLibUSB::open(uint8_t index)
	{
		m_falconDevice = libusb_open_device_with_vid_pid(NULL, FALCON_VENDOR_ID, FALCON_PRODUCT_ID);
		if(m_falconDevice == 0)
		{
			std::cout << "Cannot open device "<< std::endl;
			return false;
		}
			
		int r = libusb_claim_interface(m_falconDevice, 0);
		if (r < 0) {
			std::cout << "usb_claim_interface error " << r << std::endl;
			return false;
		}
		out_transfer = libusb_alloc_transfer(0);
		if (!out_transfer)
		{
			std::cout << "Cannot allocate out transfer" << std::endl;
			return false;
		}
		in_transfer = libusb_alloc_transfer(0);
		if (!in_transfer)
		{
			std::cout << "Cannot allocate in transfer\n" << std::endl;
			return false;
		}
		m_isCommOpen = true;
		return true;
	}

	bool FalconCommLibUSB::close()
	{
		if(!m_isCommOpen)
		{
			m_errorCode = FALCON_COMM_DEVICE_NOT_VALID_ERROR;
			return false;
		}

		libusb_close(m_falconDevice);
		return true;
	}

	void FalconCommLibUSB::setHasBytesAvailable(bool v)
	{
		m_hasBytesAvailable = true;
	}
	
	bool FalconCommLibUSB::read(uint8_t* buffer, u_int32_t size)
	{
		if(!m_isCommOpen)
		{
			m_errorCode = FALCON_COMM_DEVICE_NOT_VALID_ERROR;
			return false;
		}

		//Plus 2. Stupid modem bits.
		if(size > 0)
			memcpy(buffer, output+2, size);
		m_hasBytesAvailable = false;
		m_bytesAvailable -= size;
		return true;
	}
	
	bool FalconCommLibUSB::write(uint8_t* buffer, u_int32_t size)
	{
		if(!m_isCommOpen)
		{
			m_errorCode = FALCON_COMM_DEVICE_NOT_VALID_ERROR;
			return false;
		}

		libusb_fill_bulk_transfer(in_transfer, m_falconDevice, 0x02, buffer,
								  size, FalconCommLibUSB::cb_in, NULL, 0);
		libusb_submit_transfer(in_transfer);

		//Try to read over 64 and you'll fry libusb-1.0. Granted, the endpoint
		//limits that anyways, but still.
		libusb_fill_bulk_transfer(out_transfer, m_falconDevice, 0x81, output,
								  64, FalconCommLibUSB::cb_out, this, 1000);
		libusb_submit_transfer(out_transfer);
		return true;
	}

	bool FalconCommLibUSB::setFirmwareMode()
	{
		unsigned int bytes_written, bytes_read;
		unsigned char check_msg_1_send[3] = {0x0a, 0x43, 0x0d};
		unsigned char check_msg_1_recv[4] = {0x0a, 0x44, 0x2c, 0x0d};
		unsigned char check_msg_2[1] = {0x41};
		unsigned char send_buf[128], receive_buf[128];
		int k;
	
		if(!m_isCommOpen)
		{
			m_errorCode = FALCON_COMM_DEVICE_NOT_VALID_ERROR;
			return false;
		}

		//Save ourselves having to reset this on every error
		m_errorCode = FALCON_COMM_DEVICE_ERROR;

		//Clear out current buffers to make sure we have a fresh start
		//if((m_deviceErrorCode = ftdi_usb_purge_buffers(&(m_falconDevice))) < 0) return false;
		if ((m_deviceErrorCode = libusb_control_transfer(m_falconDevice, SIO_RESET_REQUEST_TYPE, SIO_RESET_REQUEST, SIO_RESET_PURGE_RX, INTERFACE_ANY, NULL, 0, 1000)) != 0) return false;

		if ((m_deviceErrorCode = libusb_control_transfer(m_falconDevice, SIO_RESET_REQUEST_TYPE, SIO_RESET_REQUEST, SIO_RESET_PURGE_TX, INTERFACE_ANY, NULL, 0, 1000)) != 0) return false;
		//Reset the device
		//if((m_deviceErrorCode = ftdi_usb_reset(&(m_falconDevice))) < 0) return false;
		
		//Make sure our latency timer is at 16ms, otherwise firmware checks tend to always fail
		//if((m_deviceErrorCode = ftdi_set_latency_timer(&(m_falconDevice), 16)) < 0) return false;		
	    if ((m_deviceErrorCode = libusb_control_transfer(m_falconDevice, 0x40, 0x09, 16, INTERFACE_ANY, NULL, 0, 1000)) != 0) return false;

		//Set to:
		// 9600 baud
		// 8n1
		// No Flow Control
		// RTS Low
		// DTR High	

		//if((m_deviceErrorCode = ftdi_set_baudrate(&(m_falconDevice), 9600)) < 0) return false;
		//Baud for 9600 is 0x4138. Just trust me. It is.
		if ((m_deviceErrorCode = libusb_control_transfer(m_falconDevice, SIO_SET_BAUDRATE_REQUEST_TYPE, SIO_SET_BAUDRATE_REQUEST, 0x4138, INTERFACE_ANY, NULL, 0, 1000)) != 0) return false;

		//if((m_deviceErrorCode = ftdi_set_line_property(&(m_falconDevice), BITS_8, STOP_BIT_1, NONE)) < 0) return false;
		//BITS_8 = 8, STOP_BIT_1 = 0, NONE = 0
		if ((m_deviceErrorCode = libusb_control_transfer(m_falconDevice, SIO_SET_DATA_REQUEST_TYPE, SIO_SET_DATA_REQUEST, (8 | (0x00 << 11) | (0x00 << 8)), INTERFACE_ANY, NULL, 0, 1000)) != 0) return false;

		//if((m_deviceErrorCode = ftdi_setflowctrl(&(m_falconDevice), SIO_DISABLE_FLOW_CTRL)) < 0) return false;
		if ((m_deviceErrorCode = libusb_control_transfer(m_falconDevice, SIO_SET_FLOW_CTRL_REQUEST_TYPE, SIO_SET_FLOW_CTRL_REQUEST, 0, INTERFACE_ANY, NULL, 0, 1000)) != 0) return false;

		//if((m_deviceErrorCode = ftdi_setrts(&(m_falconDevice), 0)) < 0) return false;
		if ((m_deviceErrorCode = libusb_control_transfer(m_falconDevice, SIO_SET_MODEM_CTRL_REQUEST_TYPE, SIO_SET_MODEM_CTRL_REQUEST, SIO_SET_RTS_LOW, INTERFACE_ANY, NULL, 0, 1000)) != 0) return false;

		//if((m_deviceErrorCode = ftdi_setdtr(&(m_falconDevice), 0)) < 0) return false;
		if ((m_deviceErrorCode = libusb_control_transfer(m_falconDevice, SIO_SET_MODEM_CTRL_REQUEST_TYPE, SIO_SET_MODEM_CTRL_REQUEST, SIO_SET_DTR_LOW, INTERFACE_ANY, NULL, 0, 1000)) != 0) return false;

		//if((m_deviceErrorCode = ftdi_setdtr(&(m_falconDevice), 1)) < 0) return false;
		if ((m_deviceErrorCode = libusb_control_transfer(m_falconDevice, SIO_SET_MODEM_CTRL_REQUEST_TYPE, SIO_SET_MODEM_CTRL_REQUEST, SIO_SET_DTR_HIGH, INTERFACE_ANY, NULL, 0, 1000)) != 0) return false;

		//Send 3 bytes: 0x0a 0x43 0x0d
		int transferred;
		if((m_deviceErrorCode = libusb_bulk_transfer(m_falconDevice, 0x2, check_msg_1_send, 3, &transferred, 1000)) != 0) return false;
		if((m_deviceErrorCode = libusb_bulk_transfer(m_falconDevice, 0x81, receive_buf, 5, &transferred, 1000)) != 0) return false;	
	
		//Set to:
		// DTR Low
		// 140000 baud (0x15 clock ticks per signal)

		//if((m_deviceErrorCode = ftdi_setdtr(&(m_falconDevice),0)) < 0) return false;
		if ((m_deviceErrorCode = libusb_control_transfer(m_falconDevice, SIO_SET_MODEM_CTRL_REQUEST_TYPE, SIO_SET_MODEM_CTRL_REQUEST, SIO_SET_DTR_LOW, INTERFACE_ANY, NULL, 0, 1000)) != 0) return false;
		   
		//if((m_deviceErrorCode = ftdi_set_baudrate(&(m_falconDevice), 140000)) < 0) return false;
		if (m_deviceErrorCode = libusb_control_transfer(m_falconDevice, SIO_SET_BAUDRATE_REQUEST_TYPE, SIO_SET_BAUDRATE_REQUEST, 0x15, INTERFACE_ANY, NULL, 0, 1000) != 0) return false;

		//Send "A" character
		if((m_deviceErrorCode = libusb_bulk_transfer(m_falconDevice, 0x2, check_msg_2, 1, &transferred, 1000)) != 0) return false;
		//Expect back 2 bytes:
		// 0x13 0x41
		
		if((m_deviceErrorCode = libusb_bulk_transfer(m_falconDevice, 0x81, receive_buf, 5, &transferred, 1000)) != 0) return false;	

		m_errorCode = 0;

		return true;
	}

	bool FalconCommLibUSB::setNormalMode()
	{
		if(!m_isCommOpen)
		{
			m_errorCode = FALCON_COMM_DEVICE_NOT_VALID_ERROR;
			return false;
		}
		m_errorCode = FALCON_COMM_DEVICE_ERROR;
		//if((m_deviceErrorCode = ftdi_set_latency_timer(&(m_falconDevice), 1)) < 0) return false;
		if (m_deviceErrorCode = libusb_control_transfer(m_falconDevice, 0x40, 0x09, 1, INTERFACE_ANY, NULL, 0, 1000) != 0) return false;

	    //if (usb_control_msg(m_falconDevice, 0x40, 0x09, usb_val, ftdi->index, NULL, 0, ftdi->usb_write_timeout) != 0)
		if (m_deviceErrorCode = libusb_control_transfer(m_falconDevice, SIO_SET_BAUDRATE_REQUEST_TYPE, SIO_SET_BAUDRATE_REQUEST, 0x2, INTERFACE_ANY, NULL, 0, 1000) != 0) return false;
		//if((m_deviceErrorCode = ftdi_set_baudrate(&(m_falconDevice), 1456312)) < 0) return false;
		m_errorCode = 0;
		return true;

		return true;		
	}

	void FalconCommLibUSB::poll()
	{
		libusb_handle_events_timeout(NULL, &m_tv);
	}
	
	void FalconCommLibUSB::cb_in(struct libusb_transfer *transfer)
	{

	}

	void FalconCommLibUSB::cb_out(struct libusb_transfer *transfer)
	{
		//Minus 2. Stupid modem bits.
		((FalconCommLibUSB*)transfer->user_data)->setBytesAvailable(transfer->actual_length - 2);
		((FalconCommLibUSB*)transfer->user_data)->setHasBytesAvailable(true);
	}

}