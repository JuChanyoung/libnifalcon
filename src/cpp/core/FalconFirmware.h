/***
 * @file FalconFirmware.h
 * @brief Base class for firmware definition classes
 * @author Kyle Machulis (kyle@nonpolynomial.com)
 * @version $Id$
 * @copyright (c) 2007-2008 Nonpolynomial Labs/Kyle Machulis
 * @license BSD License
 *
 * $HeadURL$
 * 
 * Project info at http://libnifalcon.sourceforge.net/ 
 *
 */

#ifndef FALCONFIRMWARE_H
#define FALCONFIRMWARE_H

#include <stdint.h>
#include <cstdlib>
#include "FalconComm.h"

namespace libnifalcon
{
	class FalconFirmware : public FalconCore
	{	
	public:
		enum
		{
			GREEN_LED=0x2,
			BLUE_LED=0x4,
			RED_LED=0x8
		};

		enum
		{
			ENCODER_1_HOMED = 0x1,
			ENCODER_2_HOMED = 0x2,
			ENCODER_3_HOMED = 0x4,			
		};
		
		FalconFirmware() :
			m_falconComm(NULL),
			m_homingMode(false)
		{
			//Who needs loops!
			m_forceValues[0] = 0;
			m_forceValues[1] = 0;
			m_forceValues[2] = 0;
			m_encoderValues[0] = 0;
			m_encoderValues[1] = 0;
			m_encoderValues[2] = 0;			
		}

		virtual ~FalconFirmware()
		{
			//Don't do anything with the falcon communications
			//Assume that's managed elsewhere
		}
		virtual bool runIOLoop() = 0;
		virtual int32_t getGripInfoSize() = 0;
		virtual u_int8_t* getGripInfo() = 0;

		void setForces(int16_t force[3])
		{
			m_forceValues[0] = force[0];
			m_forceValues[1] = force[1];
			m_forceValues[2] = force[2];
		}
		int16_t* getEncoderValues() { return m_encoderValues; }
	
		void setLEDStatus(u_int8_t leds) { m_ledStatus = leds; }
		u_int8_t getLEDStatus() { return m_ledStatus; }
		void setHomingMode(bool value) { m_homingMode = value; }
		u_int8_t getHomingModeStatus() { return m_homingStatus; }	
		bool isHomed() { return ( m_homingStatus & (ENCODER_1_HOMED | ENCODER_2_HOMED | ENCODER_3_HOMED)); }
	
		void setFalconComm(FalconComm* f) { m_falconComm = f; }
	protected:
		FalconComm* m_falconComm;

		//Values sent to falcon
		bool m_homingMode;
		u_int8_t m_ledStatus;
		int16_t m_forceValues[3];	

		//Values received from falcon
		int16_t m_encoderValues[3];
		u_int8_t m_homingStatus;
	};
}
#endif
