#include "stdafx.h"
#include "DenkoviDevices.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "../httpclient/HTTPClient.h"
#include "hardwaretypes.h"
#include "../main/localtime_r.h"
#include "../main/mainworker.h"
#include "../main/SQLHelper.h"
#include <sstream>

#define MAX_POLL_INTERVAL 30*1000

#define DAE_DI_DEF		"DigitalInput"
#define DAE_AI_DEF		"AnalogInput"
#define DAE_TI_DEF		"TemperatureInput"
#define DAE_AO_DEF		"AnalogOutput"
#define DAE_PWM_DEF		"PWM"
#define DAE_OUT_DEF		"Output"
#define DAE_RELAY_DEF	"Relay"
#define DAE_CH_DEF		"Channel"


#define SDO_AI_DEF		"AI"
#define SDO_DI_DEF		"DI"
#define SDO_TI_DEF		"TI"
#define SDO_RELAY_DEF	"Relay"
#define SDO_MAIN_DEF	"Main"

#define DAENETIP3_PORTA_MDO_DEF			"ASG"//portA multiple output state
#define DAENETIP3_PORTA_SDO_DEF			"AS"//portA single output state
#define DAENETIP3_PORTA_SNAME_DEF		"AC"//portA signle output name

#define DAENETIP3_PORTB_MDI_DEF			"BVG"//portB multiple input state
#define DAENETIP3_PORTB_SDI_DEF			"BV"//portB single output state
#define DAENETIP3_PORTB_SNAME_DEF		"BC"//portB signle input name

#define DAENETIP3_PORTC_SNAME_DEF		"CC"//portC signle analog input name
#define DAENETIP3_PORTC_SDIM_DEF		"CS"//portC signle analog input dimension
#define DAENETIP3_PORTC_SVAL_DEF		"CD"//portC signle analog input value + dimension

#define MIN_DAENETIP3_RESPOND_LENGTH	105

#define DAE_VALUE_DEF		"Value"
#define DAE_STATE_DEF		"State"
#define DAE_RSTATE_DEF		"RelayState"
#define DAE_MEASURE_DEF		"Measure"
#define DAE_SCAL_VALUE_DEF	"Scaled_Value"
#define DAE_NAME_DEF		"Name"
#define DAE_LASTO_DEF		"LastOpened"
#define DAE_LASTC_DEF		"LastClosed"

#define DAENETIP3_AI_VALUE				0
#define DAENETIP3_AI_DIMENSION			1
#define DAENETIP2_PORT_3_VAL			0
#define DAENETIP2_PORT_5_VAL			1
#define DAENETIP2_AI_VOLTAGE			0
#define DAENETIP2_AI_TEMPERATURE		1

enum _eDenkoviIOType
{
	DIOType_DI = 0,		//0
	DIOType_DO,			//1
	DIOType_Relay,		//2
	DIOType_PWM,		//3
	DIOType_AO,			//4
	DIOType_AI,			//5
	DIOType_DIO,		//6
	DIOType_MCD,		//7
	DIOType_TXT			//8
};

CDenkoviDevices::CDenkoviDevices(const int ID, const std::string &IPAddress, const unsigned short usIPPort, const std::string &password, const int pollInterval, const int model) :
	m_szIPAddress(IPAddress),
	m_Password(CURLEncode::URLEncode(password)),
	m_pollInterval(pollInterval)
{
	m_HwdID = ID;
	m_usIPPort = usIPPort;
	m_stoprequested = false;
	m_bOutputLog = false;
	m_iModel = model;
	if (m_pollInterval < 500)
		m_pollInterval = 500;
	else if (m_pollInterval > MAX_POLL_INTERVAL)
		m_pollInterval = MAX_POLL_INTERVAL;
	Init();
}


CDenkoviDevices::~CDenkoviDevices()
{
}

void CDenkoviDevices::Init()
{
}

bool CDenkoviDevices::StartHardware()
{
	Init();

	//Start worker thread
	m_thread = std::make_shared<std::thread>(&CDenkoviDevices::Do_Work, this);
	//m_thread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CDenkoviDevices::Do_Work, this)));
	m_bIsStarted = true;
	sOnConnected(this);
	switch (m_iModel) {
	case DDEV_DAEnet_IP4:
		_log.Log(LOG_STATUS, "DAEnetIP4: Started");
		break;
	case DDEV_SmartDEN_IP_16_Relays:
		_log.Log(LOG_STATUS, "SmartDEN IP-16R: Started");
		break;
	case DDEV_SmartDEN_IP_32_In:
		_log.Log(LOG_STATUS, "SmartDEN IP-32IN: Started");
		break;
	case DDEV_SmartDEN_IP_Maxi:
		_log.Log(LOG_STATUS, "SmartDEN IP-Maxi: Started");
		break;
	case DDEV_SmartDEN_IP_Watchdog:
		_log.Log(LOG_STATUS, "SmartDEN IP-Watchdog: Started");
		break;
	case DDEV_SmartDEN_Logger:
		_log.Log(LOG_STATUS, "SmartDEN Logger: Started");
		break;
	case DDEV_SmartDEN_Notifier:
		_log.Log(LOG_STATUS, "SmartDEN Notifier: Started");
		break;
	case DDEV_DAEnet_IP3:
		_log.Log(LOG_STATUS, "DAEnetIP3: Started");
		break;
	case DDEV_DAEnet_IP2:
		_log.Log(LOG_STATUS, "DAEnetIP2: Started");
		break;
	case DDEV_DAEnet_IP2_8_RELAYS:
		_log.Log(LOG_STATUS, "DAEnetIP2 8 Relay Module - LM35DZ: Started");
		break;
	case DDEV_SmartDEN_Opener:
		_log.Log(LOG_STATUS, "SmartDEN Opener: Started");
		break;
	}
	return (m_thread != NULL);
}

bool CDenkoviDevices::StopHardware()
{
	if (m_thread != NULL)
	{
		assert(m_thread);
		m_stoprequested = true;
		m_thread->join();
	}
	m_bIsStarted = false;
	return true;
}

void CDenkoviDevices::Do_Work()
{
	int poll_interval = m_pollInterval / 100;
	int poll_counter = poll_interval - 2;

	while (!m_stoprequested)
	{
		sleep_milliseconds(100);
		poll_counter++;

		if (poll_counter % 12 * 10 == 0) { //10 steps = 1 second (10 * 100)
			m_LastHeartbeat = mytime(NULL);
		}

		if (poll_counter % poll_interval == 0)
		{
			GetMeterDetails();
		}
	}
	switch (m_iModel) {
	case DDEV_DAEnet_IP4:
		_log.Log(LOG_STATUS, "DAEnetIP4: Worker stopped...");
		break;
	case DDEV_SmartDEN_IP_16_Relays:
		_log.Log(LOG_STATUS, "SmartDEN IP-16R: Worker stopped...");
		break;
	case DDEV_SmartDEN_IP_32_In:
		_log.Log(LOG_STATUS, "SmartDEN IP-32IN: Worker stopped...");
		break;
	case DDEV_SmartDEN_IP_Maxi:
		_log.Log(LOG_STATUS, "SmartDEN IP-Maxi: Worker stopped...");
		break;
	case DDEV_SmartDEN_IP_Watchdog:
		_log.Log(LOG_STATUS, "SmartDEN IP-Watchdog: Worker stopped...");
		break;
	case DDEV_SmartDEN_Logger:
		_log.Log(LOG_STATUS, "SmartDEN Logger: Worker stopped...");
		break;
	case DDEV_SmartDEN_Notifier:
		_log.Log(LOG_STATUS, "SmartDEN Notifier: Worker stopped...");
		break;
	case DDEV_DAEnet_IP3:
		_log.Log(LOG_STATUS, "DAEnetIP3: Worker stopped...");
		break;
	case DDEV_DAEnet_IP2:
		_log.Log(LOG_STATUS, "DAEnetIP2: Worker stopped...");
		break;
	case DDEV_DAEnet_IP2_8_RELAYS:
		_log.Log(LOG_STATUS, "DAEnetIP2 8 Relay Module - LM35DZ: Worker stopped...");
		break;
	case DDEV_SmartDEN_Opener:
		_log.Log(LOG_STATUS, "SmartDEN Opener: Worker stopped...");
		break;
	}
}

bool CDenkoviDevices::WriteToHardware(const char *pdata, const unsigned char length)
{
	const _tGeneralSwitch *pSen = reinterpret_cast<const _tGeneralSwitch*>(pdata);

	if (m_Password.empty())
	{
		switch (m_iModel) {
		case DDEV_DAEnet_IP4:
			_log.Log(LOG_ERROR, "DAEnetIP4: Please enter a password.");
			break;
		case DDEV_SmartDEN_IP_16_Relays:
			_log.Log(LOG_ERROR, "SmartDEN IP-16R: Please enter a password.");
			break;
		case DDEV_SmartDEN_IP_32_In:
			_log.Log(LOG_ERROR, "SmartDEN IP-32IN: Please enter a password.");
			break;
		case DDEV_SmartDEN_IP_Maxi:
			_log.Log(LOG_ERROR, "SmartDEN IP-Maxi: Please enter a password.");
			break;
		case DDEV_SmartDEN_IP_Watchdog:
			_log.Log(LOG_ERROR, "SmartDEN IP-Watchdog: Please enter a password.");
			break;
		case DDEV_SmartDEN_Logger:
			_log.Log(LOG_ERROR, "SmartDEN Logger: Please enter a password.");
			break;
		case DDEV_SmartDEN_Notifier:
			_log.Log(LOG_ERROR, "SmartDEN Notifier: Please enter a password.");
			break;
		case DDEV_DAEnet_IP3:
			_log.Log(LOG_ERROR, "DAEnetIP3: Please enter a password.");
			break;
		case DDEV_DAEnet_IP2:
			_log.Log(LOG_ERROR, "DAEnetIP2: Please enter a password.");
			break;
		case DDEV_DAEnet_IP2_8_RELAYS:
			_log.Log(LOG_ERROR, "DAEnetIP2 8 Relay Module - LM35DZ: Please enter a password.");
			break;
		case DDEV_SmartDEN_Opener:
			_log.Log(LOG_ERROR, "SmartDEN Opener: Please enter a password.");
			break;
		}
		return false;
	}

	switch (m_iModel) {
	case DDEV_SmartDEN_Opener: {
		int ioType = pSen->id;
		if ((ioType != DIOType_MCD) && (ioType != DIOType_Relay))
		{
			_log.Log(LOG_ERROR, "SmartDEN Opener: Not a valid Relay or Main Controlled Device!");
			return false;
		}
		int io = pSen->unitcode;//Relay1, Relay2 and MCD
		if (io > 3)
			return false;

		std::stringstream szURL;

		szURL << "http://" << m_szIPAddress << ":" << m_usIPPort << "/current_state.xml";
		if (!m_Password.empty())
			szURL << "?pw=" << m_Password << "&";
		else
			szURL << "?";

		if (ioType == DIOType_Relay)
			szURL << "R" << io << "=";
		else //DIOType_PWM:
			szURL << "MS=";

		if (ioType == DIOType_Relay) {
			if (pSen->cmnd == light2_sOff)
				szURL << "0";
			else if (pSen->cmnd == light2_sOn)
				szURL << "1";
			else {
				_log.Log(LOG_ERROR, "SmartDEN Opener: Not a valid command for Relay!");
				return false;
			}
		}
		else if (ioType == DIOType_MCD) {
			if (pSen->cmnd == blinds_sOpen)
				szURL << "1";
			else if (pSen->cmnd == blinds_sClose)
				szURL << "0";
			else if (pSen->cmnd == 0x11) //stop button
				szURL << "2";
			else {
				_log.Log(LOG_ERROR, "SmartDEN Opener: Not a valid command for MCD!");
				return false;
			}
		}

		std::string sResult;
		if (!HTTPClient::GET(szURL.str(), sResult)) {
			_log.Log(LOG_ERROR, "SmartDEN Opener: Error sending command to: %s", m_szIPAddress.c_str());
			return false;
		}
		if (sResult.find("Message") != std::string::npos) {
			_log.Log(LOG_ERROR, "SmartDEN Opener: Error sending command to: %s", m_szIPAddress.c_str());
			return false;
		}
		return true;
	}
	case DDEV_DAEnet_IP2_8_RELAYS: {
		std::string sPass = m_Password;
		sPass.replace(sPass.find("%3A"), 3, ":");
		int ioType = pSen->id;
		if (ioType != DIOType_DIO && ioType != DIOType_Relay)
		{
			_log.Log(LOG_ERROR, "DAEnetIP2 8 Relay Module - LM35DZ: Not a valid Digital Input/Output or Relay! ");
			return false;
		}
		int io = pSen->unitcode;//DIO1 to DIO16
		if (io > 16)
			return false;

		std::stringstream szURL;
		std::string sResult;

		szURL << "http://" << sPass << "@" << m_szIPAddress << ":" << m_usIPPort << "/ioreg.js";
		if (!HTTPClient::GET(szURL.str(), sResult)) {
			_log.Log(LOG_ERROR, "DAEnetIP2 8 Relay Module - LM35DZ: Error sending command to: %s", m_szIPAddress.c_str());
			return false;
		}
		uint8_t port3 = DAEnetIP2GetIoPort(sResult, DAENETIP2_PORT_3_VAL);
		uint8_t port5 = DAEnetIP2GetIoPort(sResult, DAENETIP2_PORT_5_VAL);
		if (io < 9) { //DIO1 to DIO8 are from Port 3
			if (pSen->cmnd == light2_sOff)
				port3 = port3 & (~(0x01 << (io - 1)));
			else if (pSen->cmnd == light2_sOn)
				port3 = port3 | (0x01 << (io - 1));
			else {
				_log.Log(LOG_ERROR, "DAEnetIP2 8 Relay Module - LM35DZ: Not a valid command. Digital Input/Output could be On or Off.");
				return false;
			}
		}
		else { //DIO9 to DIO16 are from Port 5
			if (pSen->cmnd == light2_sOff)
				port5 = port5 & (~(0x01 << (io - 8 - 1)));
			else if (pSen->cmnd == light2_sOn)
				port5 = port5 | (0x01 << (io - 8 - 1));
			else {
				_log.Log(LOG_ERROR, "DAEnetIP2 8 Relay Module - LM35DZ: Not a valid command. Digital Input/Output could be On or Off.");
				return false;
			}
		}
		szURL.str("");
		char port3Val[3], port5Val[3];
		sprintf(port3Val, "%02X", port3);
		sprintf(port5Val, "%02X", port5);
		szURL << "http://" << sPass << "@" << m_szIPAddress << ":" << m_usIPPort << "/iochange.cgi?ref=re-io&01=" << port3Val << "&02=" << port5Val;
		if (!HTTPClient::GET(szURL.str(), sResult)) {
			_log.Log(LOG_ERROR, "DAEnetIP2 8 Relay Module - LM35DZ: Error sending command to: %s", m_szIPAddress.c_str());
			return false;
		}
		if (sResult.find("window.location=\"io.htm\"") == std::string::npos) {
			_log.Log(LOG_ERROR, "DAEnetIP2 8 Relay Module - LM35DZ: Error setting Digital Input/Output %u", io);
			return false;
		}
		return true;
	}
	case DDEV_DAEnet_IP2: {
		std::string sPass = m_Password;
		sPass.replace(sPass.find("%3A"), 3, ":");
		int ioType = pSen->id;
		if (ioType != DIOType_DIO)
		{
			_log.Log(LOG_ERROR, "DAEnetIP2: Not a valid Digital Input/Output! ");
			return false;
		}
		int io = pSen->unitcode;//DIO1 to DIO16
		if (io > 16)
			return false;

		std::stringstream szURL;
		std::string sResult;

		szURL << "http://" << sPass << "@" << m_szIPAddress << ":" << m_usIPPort << "/ioreg.js";
		if (!HTTPClient::GET(szURL.str(), sResult)) {
			_log.Log(LOG_ERROR, "DAEnetIP2: Error sending command to: %s", m_szIPAddress.c_str());
			return false;
		}
		uint8_t port3 = DAEnetIP2GetIoPort(sResult, DAENETIP2_PORT_3_VAL);
		uint8_t port5 = DAEnetIP2GetIoPort(sResult, DAENETIP2_PORT_5_VAL);
		if (io < 9) { //DIO1 to DIO8 are from Port 3
			if (pSen->cmnd == light2_sOff)
				port3 = port3 & (~(0x01 << (io - 1)));
			else if (pSen->cmnd == light2_sOn)
				port3 = port3 | (0x01 << (io - 1));
			else {
				_log.Log(LOG_ERROR, "DAEnetIP2: Not a valid command. Digital Input/Output could be On or Off.");
				return false;
			}
		}
		else { //DIO9 to DIO16 are from Port 5
			if (pSen->cmnd == light2_sOff)
				port5 = port5 & (~(0x01 << (io - 8 - 1)));
			else if (pSen->cmnd == light2_sOn)
				port5 = port5 | (0x01 << (io - 8 - 1));
			else {
				_log.Log(LOG_ERROR, "DAEnetIP2: Not a valid command. Digital Input/Output could be On or Off.");
				return false;
			}
		}
		szURL.str("");
		char port3Val[3], port5Val[3];
		sprintf(port3Val, "%02X", port3);
		sprintf(port5Val, "%02X", port5);
		szURL << "http://" << sPass << "@" << m_szIPAddress << ":" << m_usIPPort << "/iochange.cgi?ref=re-io&01=" << port3Val << "&02=" << port5Val;
		if (!HTTPClient::GET(szURL.str(), sResult)) {
			_log.Log(LOG_ERROR, "DAEnetIP2: Error sending command to: %s", m_szIPAddress.c_str());
			return false;
		}
		if (sResult.find("window.location=\"io.htm\"") == std::string::npos) {
			_log.Log(LOG_ERROR, "DAEnetIP2: Error setting Digital Input/Output %u", io);
			return false;
		}
		return true;
	}
	case DDEV_DAEnet_IP3: {
		int ioType = pSen->id;
		if (ioType != DIOType_DO)
		{
			_log.Log(LOG_ERROR, "DAEnetIP3: Not a valid Digital Output! ");
			return false;
		}
		int io = pSen->unitcode;//DO1 to DO16
		if (io > 16)
			return false;

		std::stringstream szURL;

		szURL << "http://" << m_szIPAddress << ":" << m_usIPPort << "/command.html";
		if (!m_Password.empty())
			szURL << "?P=" << m_Password << "&";
		else
			szURL << "?";

		szURL << DAENETIP3_PORTA_SDO_DEF << std::uppercase << std::hex << io - 1 << "=";
		if (pSen->cmnd == light2_sOff)
			szURL << "0&";
		else if (pSen->cmnd == light2_sOn) {
			szURL << "1&";
		}
		else {
			_log.Log(LOG_ERROR, "DAEnetIP3: Not a valid command. Digital Output could be On or Off.");
			return false;
		}
		std::string sResult;
		if (!HTTPClient::GET(szURL.str(), sResult)) {
			_log.Log(LOG_ERROR, "DAEnetIP3: Error sending command to: %s", m_szIPAddress.c_str());
			return false;
		}
		szURL.str("");
		szURL << DAENETIP3_PORTA_SDO_DEF << std::uppercase << std::hex << io - 1;
		if (sResult.find(szURL.str()) == std::string::npos) {
			_log.Log(LOG_ERROR, "DAEnetIP3: Error setting Digital Output %u", io);
			return false;
		}
		return true;

	}
	case DDEV_SmartDEN_Notifier: {
		_log.Log(LOG_ERROR, "SmartDEN Notifier: This board does not have outputs! ");
		return false;
	}
	case DDEV_SmartDEN_Logger: {
		_log.Log(LOG_ERROR, "SmartDEN Logger: This board does not have outputs! ");
		return false;
	}
	case DDEV_SmartDEN_IP_32_In: {
		_log.Log(LOG_ERROR, "SmartDEN IP-32IN: This board does not have outputs! ");
		return false;
	}
	case DDEV_SmartDEN_IP_Maxi: {
		int ioType = pSen->id;
		if ((ioType != DIOType_AO) && (ioType != DIOType_Relay))
		{
			_log.Log(LOG_ERROR, "SmartDEN IP-Maxi: Not a valid Relay or Analog Output!");
			return false;
		}
		int io = pSen->unitcode;//Relay1 to Relay8 and AO1 to AO2
		if (io > 8)
			return false;

		std::stringstream szURL;

		szURL << "http://" << m_szIPAddress << ":" << m_usIPPort << "/current_state.xml";
		if (!m_Password.empty())
			szURL << "?pw=" << m_Password << "&";
		else
			szURL << "?";

		if (ioType == DIOType_Relay)
			szURL << "Relay" << io << "=";
		else //DIOType_PWM:
			szURL << "AnalogOutput" << io << "=";

		if (pSen->cmnd == light2_sOff)
			szURL << "0";
		else if (pSen->cmnd == light2_sSetLevel) {
			double lvl = pSen->level*10.23;
			int iLvl = (int)lvl;
			szURL << std::to_string(iLvl);
		}
		else
			szURL << "1";
		std::string sResult;
		if (!HTTPClient::GET(szURL.str(), sResult)) {
			_log.Log(LOG_ERROR, "SmartDEN IP-Maxi: Error sending command to: %s", m_szIPAddress.c_str());
			return false;
		}
		if (sResult.find("CurrentState") == std::string::npos) {
			_log.Log(LOG_ERROR, "SmartDEN IP-Maxi: Error sending command to: %s", m_szIPAddress.c_str());
			return false;
		}
		return true;
	}
	case DDEV_DAEnet_IP4: {
		int ioType = pSen->id;
		if ((ioType != DIOType_DO) && (ioType != DIOType_PWM))
		{
			_log.Log(LOG_ERROR, "DAEnetIP4: Not a valid Digital or PWM output");
			return false;
		}
		int io = pSen->unitcode;//Output1 to Output16 and PWM1 to PWM2
		if (io > 16)
			return false;

		std::stringstream szURL;

		szURL << "http://" << m_szIPAddress << ":" << m_usIPPort << "/current_state.xml";
		if (!m_Password.empty())
			szURL << "?pw=" << m_Password << "&";
		else
			szURL << "?";

		if (ioType == DIOType_DO)
			szURL << "Output" << io << "=";
		else //DIOType_PWM:
			szURL << "PWM" << io << "=";

		if (pSen->cmnd == light2_sOff)
			szURL << "0";
		else if (pSen->cmnd == light2_sSetLevel)
			szURL << std::to_string(pSen->level);
		else
			szURL << "1";
		std::string sResult;
		if (!HTTPClient::GET(szURL.str(), sResult)) {
			_log.Log(LOG_ERROR, "DAEnetIP4: Error sending command to: %s", m_szIPAddress.c_str());
			return false;
		}
		if (sResult.find("CurrentState") == std::string::npos) {
			_log.Log(LOG_ERROR, "DAEnetIP4: Error sending command to: %s", m_szIPAddress.c_str());
			return false;
		}
		return true;
	}
	case DDEV_SmartDEN_IP_Watchdog:
	case DDEV_SmartDEN_IP_16_Relays: {
		int ioType = pSen->id;
		if (ioType != DIOType_Relay)
		{
			if (m_iModel == DDEV_SmartDEN_IP_Watchdog)
				_log.Log(LOG_ERROR, "SmartDEN IP-Watchdog: Not a valid Relay switch.");
			else
				_log.Log(LOG_ERROR, "SmartDEN IP-16R: Not a valid Relay switch.");
			return false;
		}
		int Relay = pSen->unitcode;
		if (Relay > 16)
			return false;

		std::stringstream szURL;

		szURL << "http://" << m_szIPAddress << ":" << m_usIPPort << "/current_state.xml";
		if (!m_Password.empty())
			szURL << "?pw=" << m_Password << "&Relay" << Relay << "=";
		else
			szURL << "?Relay" << Relay << "=";

		if (pSen->cmnd == light2_sOff)
			szURL << "0";
		else if (pSen->cmnd == light2_sOn) {
			szURL << "1";
		}
		else {
			if (m_iModel == DDEV_SmartDEN_IP_Watchdog)
				_log.Log(LOG_ERROR, "SmartDEN IP-Watchdog: Not a valid command. Relay could be On or Off.");
			else
				_log.Log(LOG_ERROR, "SmartDEN IP-16R: Not a valid command. Relay could be On or Off.");
			return false;
		}
		std::string sResult;
		if (!HTTPClient::GET(szURL.str(), sResult))
		{
			if (m_iModel == DDEV_SmartDEN_IP_Watchdog)
				_log.Log(LOG_ERROR, "SmartDEN IP-Watchdog: Error sending relay command to: %s", m_szIPAddress.c_str());
			else
				_log.Log(LOG_ERROR, "SmartDEN IP-16R: Error sending relay command to: %s", m_szIPAddress.c_str());
			return false;
		}
		if (sResult.find("CurrentState") == std::string::npos)
		{
			if (m_iModel == DDEV_SmartDEN_IP_Watchdog)
				_log.Log(LOG_ERROR, "SmartDEN IP-Watchdog: Error sending relay command to: %s", m_szIPAddress.c_str());
			else
				_log.Log(LOG_ERROR, "SmartDEN IP-16R: Error sending relay command to: %s", m_szIPAddress.c_str());
			return false;
		}
		return true;
	}
	}
	_log.Log(LOG_ERROR, "Denkovi: Unknown Device!");
	return false;
}

int CDenkoviDevices::DenkoviCheckForIO(std::string tmpstr, const std::string &tmpIoType) {
	int pos1;
	int Idx = -1;
	std::string ioType = "<" + tmpIoType;
	pos1 = tmpstr.find(ioType);
	if (pos1 != std::string::npos)
	{
		tmpstr = tmpstr.substr(pos1 + strlen(ioType.c_str()));
		pos1 = tmpstr.find(">");
		if (pos1 != std::string::npos)
			Idx = atoi(tmpstr.substr(0, pos1).c_str());
	}
	return Idx;
}

int CDenkoviDevices::DenkoviGetIntParameter(std::string tmpstr, const std::string &tmpParameter) {
	int pos1;
	int lValue = -1;
	std::string parameter = "<" + tmpParameter + ">";
	pos1 = tmpstr.find(parameter);
	if (pos1 != std::string::npos)
	{
		tmpstr = tmpstr.substr(pos1 + strlen(parameter.c_str()));
		pos1 = tmpstr.find('<');
		if (pos1 != std::string::npos)
			lValue = atoi(tmpstr.substr(0, pos1).c_str());
	}
	return lValue;
}

std::string CDenkoviDevices::DenkoviGetStrParameter(std::string tmpstr, const std::string &tmpParameter) {
	int pos1;
	std::string sMeasure = "";
	std::string parameter = "<" + tmpParameter + ">";

	pos1 = tmpstr.find(parameter);
	if (pos1 != std::string::npos)
	{
		tmpstr = tmpstr.substr(pos1 + strlen(parameter.c_str()));
		pos1 = tmpstr.find('<');
		if (pos1 != std::string::npos)
			sMeasure = tmpstr.substr(0, pos1);
	}
	return sMeasure;
}

float CDenkoviDevices::DenkoviGetFloatParameter(std::string tmpstr, const std::string &tmpParameter) {
	int pos1;
	float value = NAN;
	std::string parameter = "<" + tmpParameter + ">";

	pos1 = tmpstr.find(parameter);
	if (pos1 != std::string::npos)
	{
		tmpstr = tmpstr.substr(pos1 + strlen(parameter.c_str()));
		pos1 = tmpstr.find('<');
		if (pos1 != std::string::npos)
			value = static_cast<float>(atof(tmpstr.substr(0, pos1).c_str()));
	}
	return value;
}

std::string CDenkoviDevices::DAEnetIP3GetIo(std::string tmpstr, const std::string &tmpParameter) {
	std::string parameter = tmpParameter + "=";
	int pos1 = tmpstr.find(parameter);
	int pos2 = tmpstr.find(";", pos1);
	return tmpstr.substr(pos1 + strlen(parameter.c_str()), pos2 - (pos1 + strlen(parameter.c_str()))).c_str();
}

std::string CDenkoviDevices::DAEnetIP3GetAi(std::string tmpstr, const std::string &tmpParameter, const int &ciType) {
	std::string parameter = tmpParameter + "=";
	int pos1 = tmpstr.find(parameter);
	int pos2;
	if (ciType == DAENETIP3_AI_VALUE) {
		pos2 = tmpstr.find("[", pos1);
		return tmpstr.substr(pos1 + strlen(parameter.c_str()), pos2 - (pos1 + strlen(parameter.c_str()))).c_str();
	}
	else if (ciType == DAENETIP3_AI_DIMENSION) {
		pos1 = tmpstr.find("[", pos1);
		pos2 = tmpstr.find("]", pos1);
		return tmpstr.substr(pos1 + 1, pos2 - (pos1 + 1)).c_str();
	}
	return "";// tmpstr.substr(pos1 + strlen(parameter.c_str()), pos2 - (pos1 + strlen(parameter.c_str()))).c_str();
}

uint8_t CDenkoviDevices::DAEnetIP2GetIoPort(std::string tmpstr, const int &port) {
	std::stringstream ss;
	int pos1, pos2;
	int b;
	if (port == DAENETIP2_PORT_3_VAL) {
		pos1 = tmpstr.find("(0x");
		pos2 = tmpstr.find(",", pos1);
		ss << std::hex << tmpstr.substr(pos1 + 3, pos2 - (pos1 + 3)).c_str();
		ss >> b;
		return b;
	}
	else if (port == DAENETIP2_PORT_5_VAL) {
		pos1 = tmpstr.find(",0x");
		pos2 = tmpstr.find(",", pos1 + 3);
		ss << std::hex << tmpstr.substr(pos1 + 3, pos2 - (pos1 + 3)).c_str();
		ss >> b;
		return b;
	}
	return 0;
}

std::string CDenkoviDevices::DAEnetIP2GetName(std::string tmpstr, const int &nmr) {//nmr should be from 1 to 24
	int pos1 = 0, pos2 = 0;
	for (uint8_t ii = 0; ii < (((nmr - 1) * 2) + 1); ii++) {
		pos1 = tmpstr.find("\"", pos1 + 1);
	}
	pos2 = tmpstr.find("\"", pos1 + 1);
	return tmpstr.substr(pos1 + 1, pos2 - (pos1 + 1)).c_str();
}

uint16_t CDenkoviDevices::DAEnetIP2GetAiValue(std::string tmpstr, const int &aiNmr) {
	int pos1 = 0, pos2 = 0;
	std::stringstream ss;
	int b = 0;
	for (uint8_t ii = 0; ii < aiNmr + 3; ii++) {
		pos1 = tmpstr.find("0x", pos1 + 2);
	}
	pos2 = tmpstr.find(",", pos1 + 2);
	ss << std::hex << tmpstr.substr(pos1 + 2, pos2 - (pos1 + 2)).c_str();
	ss >> b;
	return b;
}



float CDenkoviDevices::DAEnetIP2CalculateAi(int adc, const int &valType) {
	if (valType == DAENETIP2_AI_TEMPERATURE) {
		return (10000 * ((1.2*204.8)*adc / (120 * 1024) + 0) / 100);
	}
	else if (valType == DAENETIP2_AI_VOLTAGE) {
		return (10000 * ((1.2*0.377)*adc / (4.7 * 1024) + 0) / 100);
	}
	return 0;
}

void CDenkoviDevices::GetMeterDetails()
{
	std::string sResult, sResult2;
	std::stringstream szURL, szURL2;

	if (m_Password.empty())
	{
		switch (m_iModel) {
		case DDEV_DAEnet_IP4:
			_log.Log(LOG_ERROR, "DAEnetIP4: Please enter a password.");
			break;
		case DDEV_SmartDEN_IP_16_Relays:
			_log.Log(LOG_ERROR, "SmartDEN IP-16R: Please enter a password.");
			break;
		case DDEV_SmartDEN_IP_32_In:
			_log.Log(LOG_ERROR, "SmartDEN IP-32IN: Please enter a password.");
			break;
		case DDEV_SmartDEN_IP_Maxi:
			_log.Log(LOG_ERROR, "SmartDEN IP-Maxi: Please enter a password.");
			break;
		case DDEV_SmartDEN_IP_Watchdog:
			_log.Log(LOG_ERROR, "SmartDEN IP-Watchdog: Please enter a password.");
			break;
		case DDEV_SmartDEN_Logger:
			_log.Log(LOG_ERROR, "SmartDEN Logger: Please enter a password.");
			break;
		case DDEV_SmartDEN_Notifier:
			_log.Log(LOG_ERROR, "SmartDEN Notifier: Please enter a password.");
			break;
		case DDEV_DAEnet_IP3:
			_log.Log(LOG_ERROR, "DAEnetIP3: Please enter a password.");
			break;
		case DDEV_DAEnet_IP2:
			_log.Log(LOG_ERROR, "DAEnetIP2: Please enter a password.");
			break;
		case DDEV_DAEnet_IP2_8_RELAYS:
			_log.Log(LOG_ERROR, "DAEnetIP2 8 Relay Module - LM35DZ: Please enter a password.");
			break;
		case DDEV_SmartDEN_Opener:
			_log.Log(LOG_ERROR, "SmartDEN Opener: Please enter a password.");
			break;
		}
		return;
	}

	if (m_iModel == DDEV_DAEnet_IP2 || m_iModel == DDEV_DAEnet_IP2_8_RELAYS) {
		std::string sPass = m_Password;
		if (sPass.find("%3A") == std::string::npos) {
			if (m_iModel == DDEV_DAEnet_IP2)
				_log.Log(LOG_ERROR, "DAEnetIP2: Please enter username and password in format username:password. Example admin:admin.");
			else if (m_iModel == DDEV_DAEnet_IP2_8_RELAYS)
				_log.Log(LOG_ERROR, "DAEnetIP2 8 Relay Module - LM35DZ: Please enter username and password in format username:password. Example admin:admin.");
			return;
		}
		sPass.replace(sPass.find("%3A"), 3, ":");
		szURL << "http://" << sPass << "@" << m_szIPAddress << ":" << m_usIPPort << "/ioreg.js";
	}
	else if (m_iModel == DDEV_DAEnet_IP3) {
		szURL << "http://" << m_szIPAddress << ":" << m_usIPPort << "/command.html";
		szURL2 << "http://" << m_szIPAddress << ":" << m_usIPPort << "/command.html";
		if (!m_Password.empty()) {
			szURL << "?P=" << m_Password;
			szURL2 << "?P=" << m_Password;
		}
		szURL << "&ASG=?&BVG=?&CD0=?&CD1=?&CD2=?&CD3=?&CD4=?&CD5=?&CD6=?&CD7=?&AC0=?&AC1=?&AC2=?&AC3=?&AC4=?&AC5=?&AC6=?&AC7=?&AC8=?&AC9=?&ACA=?&ACB=?&ACC=?&ACD=?&ACE=?&ACF=?&";
		szURL2 << "&BC0=?&BC1=?&BC2=?&BC3=?&BC4=?&BC5=?&BC6=?&BC7=?&CC0=?&CC1=?&CC2=?&CC3=?&CC4=?&CC5=?&CC6=?&CC7=?&";
	}
	else {
		szURL << "http://" << m_szIPAddress << ":" << m_usIPPort << "/current_state.xml";
		if (!m_Password.empty())
			szURL << "?pw=" << m_Password;
	}

	if (!HTTPClient::GET(szURL.str(), sResult) || (m_iModel == DDEV_DAEnet_IP3 && !HTTPClient::GET(szURL2.str(), sResult2)))
	{
		switch (m_iModel) {
		case DDEV_DAEnet_IP4:
			_log.Log(LOG_ERROR, "DAEnetIP4: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_IP_16_Relays:
			_log.Log(LOG_ERROR, "SmartDEN IP-16R: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_IP_32_In:
			_log.Log(LOG_ERROR, "SmartDEN IP-32IN: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_IP_Maxi:
			_log.Log(LOG_ERROR, "SmartDEN IP-Maxi: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_IP_Watchdog:
			_log.Log(LOG_ERROR, "SmartDEN IP-Watchdog: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_Logger:
			_log.Log(LOG_ERROR, "SmartDEN Logger: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_Notifier:
			_log.Log(LOG_ERROR, "SmartDEN Notifier: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_DAEnet_IP3:
			_log.Log(LOG_ERROR, "DAEnetIP3: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_DAEnet_IP2:
			_log.Log(LOG_ERROR, "DAEnetIP2: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_Opener:
			_log.Log(LOG_ERROR, "SmartDEN Opener: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		}
		return;
	}
	std::vector<std::string> results;
	if (m_iModel == DDEV_SmartDEN_Opener)
		StringSplit(sResult, "\n", results);
	else
		StringSplit(sResult, "\r\n", results);
	int z = strlen(sResult.c_str());
	if (m_iModel != DDEV_DAEnet_IP3 && m_iModel != DDEV_DAEnet_IP2 && m_iModel != DDEV_DAEnet_IP2_8_RELAYS && results.size() < 8)
	{
		switch (m_iModel) {
		case DDEV_DAEnet_IP4:
			_log.Log(LOG_ERROR, "DAEnetIP4: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_IP_16_Relays:
			_log.Log(LOG_ERROR, "SmartDEN IP-16R: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_IP_32_In:
			_log.Log(LOG_ERROR, "SmartDEN IP-32IN: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_IP_Maxi:
			_log.Log(LOG_ERROR, "SmartDEN IP-Maxi: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_IP_Watchdog:
			_log.Log(LOG_ERROR, "SmartDEN IP-Watchdog: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_Logger:
			_log.Log(LOG_ERROR, "SmartDEN Logger: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_Notifier:
			_log.Log(LOG_ERROR, "SmartDEN Notifier: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		case DDEV_SmartDEN_Opener:
			_log.Log(LOG_ERROR, "SmartDEN Opener: Error connecting to: %s", m_szIPAddress.c_str());
			break;
		}
		return;
	}
	else if (m_iModel == DDEV_DAEnet_IP3 && (strlen(sResult.c_str()) < MIN_DAENETIP3_RESPOND_LENGTH)) {
		_log.Log(LOG_ERROR, "DAEnetIP3: Error connecting to: %s", m_szIPAddress.c_str());
		return;
	}
	else if (m_iModel == DDEV_DAEnet_IP2 && sResult.find("var IO=new Array") == std::string::npos) {
		_log.Log(LOG_ERROR, "DAEnetIP2: Error getting status from: %s", m_szIPAddress.c_str());
		return;
	}
	else if (m_iModel == DDEV_DAEnet_IP2_8_RELAYS && sResult.find("var IO=new Array") == std::string::npos) {
		_log.Log(LOG_ERROR, "DAEnetIP2 8 Relay Module - LM35DZ: Error getting status from: %s", m_szIPAddress.c_str());
		return;
	}

	if (m_iModel != DDEV_DAEnet_IP3 && m_iModel != DDEV_DAEnet_IP2 && m_iModel != DDEV_DAEnet_IP2_8_RELAYS && results[0] != "<CurrentState>")
	{
		switch (m_iModel) {
		case DDEV_DAEnet_IP4:
			_log.Log(LOG_ERROR, "DAEnetIP4: Error getting status");
			break;
		case DDEV_SmartDEN_IP_16_Relays:
			_log.Log(LOG_ERROR, "SmartDEN IP-16R: Error getting status");
			break;
		case DDEV_SmartDEN_IP_32_In:
			_log.Log(LOG_ERROR, "SmartDEN IP-32IN: Error getting status");
		case DDEV_SmartDEN_IP_Maxi:
			_log.Log(LOG_ERROR, "SmartDEN IP-Maxi: Error getting status");
			break;
		case DDEV_SmartDEN_IP_Watchdog:
			_log.Log(LOG_ERROR, "SmartDEN IP-Watchdog: Error getting status");
		case DDEV_SmartDEN_Logger:
			_log.Log(LOG_ERROR, "SmartDEN Logger: Error getting status");
			break;
		case DDEV_SmartDEN_Notifier:
			_log.Log(LOG_ERROR, "SmartDEN Notifier: Error getting status");
			break;
		case DDEV_SmartDEN_Opener:
			_log.Log(LOG_ERROR, "SmartDEN Opener: Error getting status");
			break;
		}
		return;
	}
	else if (m_iModel == DDEV_DAEnet_IP3 && ((sResult.find(DAENETIP3_PORTA_MDO_DEF) == std::string::npos) || (sResult2.find(DAENETIP3_PORTB_SNAME_DEF) == std::string::npos))) {
		_log.Log(LOG_ERROR, "DAEnetIP3: Error getting status");
		return;
	}

	size_t ii;
	std::string tmpstr;
	int tmpState;
	int tmpValue;
	float tmpTiValue = NAN;
	std::string tmpMeasure;
	std::string tmpName;
	std::string name;
	int Idx = -1;

	switch (m_iModel) {
	case DDEV_SmartDEN_Opener: {//has DI, AI, TI, Relays, MCD
		bool bHaveDigitalInput = false;
		bool bHaveMCD = false;
		bool bHaveAnalogInput = false;
		bool bHaveTemperatureInput = false;
		bool bHaveRelay = false;
		for (ii = 1; ii < results.size(); ii++)
		{
			tmpstr = stdstring_trim(results[ii]);

			if (Idx == -1) {
				if (!bHaveDigitalInput && ((Idx = DenkoviCheckForIO(tmpstr, SDO_DI_DEF)) != -1))
				{
					bHaveDigitalInput = true;
					continue;
				}

				if (!bHaveAnalogInput && ((Idx = DenkoviCheckForIO(tmpstr, SDO_AI_DEF)) != -1))
				{
					bHaveAnalogInput = true;
					continue;
				}

				if (!bHaveMCD && ((Idx = DenkoviCheckForIO(tmpstr, SDO_MAIN_DEF)) != -1))
				{
					bHaveMCD = true;
					continue;
				}

				if (!bHaveTemperatureInput && ((Idx = DenkoviCheckForIO(tmpstr, SDO_TI_DEF)) != -1))
				{
					bHaveTemperatureInput = true;
					continue;
				}

				if (!bHaveRelay && ((Idx = DenkoviCheckForIO(tmpstr, SDO_RELAY_DEF)) != -1))
				{
					bHaveRelay = true;
					continue;
				}
			}

			if ((tmpName = DenkoviGetStrParameter(tmpstr, DAE_NAME_DEF)) != "") {
				name = tmpName;
				continue;
			}

			if (bHaveDigitalInput && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_STATE_DEF)) != -1))
			{
				name = "Digital Input " + std::to_string(Idx) + " (" + name + ")";
				SendGeneralSwitch(DIOType_DI, Idx, 255, (tmpValue == 1) ? true : false, 100, name);
				Idx = -1;
				bHaveDigitalInput = false;
				continue;
			}

			if (bHaveAnalogInput && (Idx != -1) && ((tmpMeasure = DenkoviGetStrParameter(tmpstr, DAE_SCAL_VALUE_DEF)) != ""))
			{
				std::vector<std::string> vMeasure;
				StringSplit(tmpMeasure, " ", vMeasure);
				name = "Analog Input (" + name + ")";
				SendCustomSensor(Idx, 1, 255, static_cast<float>(atof(vMeasure[0].c_str())), name, "");// vMeasure[1]);
				Idx = -1;
				bHaveAnalogInput = false;
				continue;
			}

			if (bHaveTemperatureInput && (Idx != -1) && ((tmpMeasure = DenkoviGetStrParameter(tmpstr, DAE_VALUE_DEF)) != ""))
			{
				name = "Temperature Input (" + name + ")";
				std::vector<std::string> vMeasure;
				StringSplit(tmpMeasure, " ", vMeasure);
				tmpTiValue = atof(tmpMeasure.c_str());
				if (tmpMeasure.find("F") != std::string::npos)
					tmpTiValue = (float)ConvertToCelsius((double)tmpTiValue);
				SendTempSensor(Idx, 255, tmpTiValue, name);

				Idx = -1;
				bHaveTemperatureInput = false;
				continue;
			}

			if (bHaveRelay && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_STATE_DEF)) != -1))
			{
				name = "Relay " + std::to_string(Idx) + " (" + name + ")";
				SendGeneralSwitch(DIOType_Relay, Idx, 255, (tmpValue == 1) ? true : false, 100, name);
				Idx = -1;
				bHaveRelay = false;
				continue;
			}

			if (bHaveMCD && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_STATE_DEF)) != -1))
			{
				std::string tmpN = "";
				tmpN = "MCD (" + name + ")";
				//double val = (100 / 1023) * tmpValue;
				if (tmpValue == 1 || tmpValue == 3)
					tmpValue = blinds_sOpen;
				else if (tmpValue == 0 || tmpValue == 4)
					tmpValue = blinds_sClose;
				else
					tmpValue = 0x11;
				SendGeneralSwitch(DIOType_MCD, Idx, 255, tmpValue, 100, tmpN);
				ii = ii + 4;
				tmpstr = stdstring_trim(results[ii]);
				tmpMeasure = DenkoviGetStrParameter(tmpstr, DAE_LASTO_DEF);
				SendTextSensor(DIOType_TXT, ++Idx, 255, tmpMeasure, name + " Last Opened");
				ii = ii + 1;
				tmpstr = stdstring_trim(results[ii]);
				tmpMeasure = DenkoviGetStrParameter(tmpstr, DAE_LASTC_DEF);
				SendTextSensor(DIOType_TXT, ++Idx, 255, tmpMeasure, name + " Last Closed");
				Idx = -1;
				bHaveMCD = false;
				continue;
			}
		}
		break;
	}
	case DDEV_DAEnet_IP2_8_RELAYS: {
		uint8_t port3 = DAEnetIP2GetIoPort(sResult, DAENETIP2_PORT_3_VAL);
		uint8_t port5 = DAEnetIP2GetIoPort(sResult, DAENETIP2_PORT_5_VAL);
		for (ii = 0; ii < 8; ii++)//8 digital inputs/outputs from port3
		{
			tmpName = DAEnetIP2GetName(sResult, ii + 1);
			name = "Digital IO " + std::to_string(ii + 1) + " (" + tmpName + ")";
			SendGeneralSwitch(DIOType_DIO, ii + 1, 255, ((port3&(0x01 << ii)) != 0) ? true : false, 100, name);
		}
		for (ii = 0; ii < 8; ii++)//8 digital inputs/outputs from port5
		{
			tmpName = DAEnetIP2GetName(sResult, ii + 8 + 1);
			name = "Relay " + std::to_string(ii + 1) + " (" + tmpName + ")";
			SendGeneralSwitch(DIOType_Relay, ii + 1, 255, ((port5&(0x01 << ii)) != 0) ? true : false, 100, name);
		}
		for (ii = 0; ii < 8; ii++)//8 analog inputs
		{
			tmpName = DAEnetIP2GetName(sResult, ii + 16 + 1);
			tmpValue = DAEnetIP2GetAiValue(sResult, ii + 1);
			name = "Analog Input " + std::to_string(ii + 1) + " (" + tmpName + ")";
			SendCustomSensor(DIOType_AI, ii + 16 + 1, 255, static_cast<float>(tmpValue), name, "");
			//float voltageValue = (float)(10.44*(tmpValue / 1024));
			float voltageValue = DAEnetIP2CalculateAi(tmpValue, DAENETIP2_AI_VOLTAGE);
			SendVoltageSensor(DIOType_AI, ii + 24 + 1, 255, voltageValue, name);
			//float temperatureValue = (float)(2.05*(tmpValue / 1024));
			float temperatureValue = DAEnetIP2CalculateAi(tmpValue, DAENETIP2_AI_TEMPERATURE);
			SendTempSensor(ii + 1, 255, temperatureValue, name);
		}
		break;
	}
	case DDEV_DAEnet_IP2: {
		uint8_t port3 = DAEnetIP2GetIoPort(sResult, DAENETIP2_PORT_3_VAL);
		uint8_t port5 = DAEnetIP2GetIoPort(sResult, DAENETIP2_PORT_5_VAL);
		for (ii = 0; ii < 8; ii++)//8 digital inputs/outputs from port3
		{
			tmpName = DAEnetIP2GetName(sResult, ii + 1);
			name = "Digital IO " + std::to_string(ii + 1) + " (" + tmpName + ")";
			SendGeneralSwitch(DIOType_DIO, ii + 1, 255, ((port3&(0x01 << ii)) != 0) ? true : false, 100, name);
		}
		for (ii = 0; ii < 8; ii++)//8 digital inputs/outputs from port5
		{
			tmpName = DAEnetIP2GetName(sResult, ii + 8 + 1);
			name = "Digital IO " + std::to_string(ii + 8 + 1) + " (" + tmpName + ")";
			SendGeneralSwitch(DIOType_DIO, ii + 8 + 1, 255, ((port5&(0x01 << ii)) != 0) ? true : false, 100, name);
		}
		for (ii = 0; ii < 8; ii++)//8 analog inputs
		{
			tmpName = DAEnetIP2GetName(sResult, ii + 16 + 1);
			tmpValue = DAEnetIP2GetAiValue(sResult, ii + 1);
			name = "Analog Input " + std::to_string(ii + 1) + " (" + tmpName + ")";
			SendCustomSensor(DIOType_AI, ii + 16 + 1, 255, static_cast<float>(tmpValue), name, "");
		}
		break;
	}
	case DDEV_DAEnet_IP3: {
		unsigned int pins = std::stoul(DAEnetIP3GetIo(sResult, DAENETIP3_PORTA_MDO_DEF), nullptr, 16);
		for (ii = 0; ii < 16; ii++)//16 digital outputs
		{
			std::stringstream io;
			io << std::uppercase << std::hex << ii;
			tmpName = DAEnetIP3GetIo(sResult, DAENETIP3_PORTA_SNAME_DEF + io.str());
			name = "Digital Output " + std::to_string(ii + 1) + " (" + tmpName + ")";
			SendGeneralSwitch(DIOType_DO, ii + 1, 255, ((pins&(0x0001 << ii)) != 0) ? true : false, 100, name);
		}

		pins = std::stoul(DAEnetIP3GetIo(sResult, DAENETIP3_PORTB_MDI_DEF), nullptr, 16);
		for (ii = 0; ii < 8; ii++)//8 digital inputs
		{
			std::stringstream io;
			io << std::uppercase << std::hex << ii;
			tmpName = DAEnetIP3GetIo(sResult2, DAENETIP3_PORTB_SNAME_DEF + io.str());
			name = "Digital Input " + std::to_string(ii + 1) + " (" + tmpName + ")";
			SendGeneralSwitch(DIOType_DI, ii + 1, 255, ((pins&(0x01 << ii)) != 0) ? true : false, 100, name);
		}

		for (ii = 0; ii < 8; ii++)//8 analog inputs
		{
			tmpMeasure = DAEnetIP3GetAi(sResult, (DAENETIP3_PORTC_SVAL_DEF + std::to_string(ii)), DAENETIP3_AI_VALUE);
			tmpstr = DAEnetIP3GetAi(sResult, (DAENETIP3_PORTC_SVAL_DEF + std::to_string(ii)), DAENETIP3_AI_DIMENSION);
			tmpName = DAEnetIP3GetIo(sResult2, DAENETIP3_PORTC_SNAME_DEF + std::to_string(ii));
			name = "Analog Input " + std::to_string(ii + 1) + " (" + tmpName + ")";
			SendCustomSensor(DIOType_AI, ii + 1, 255, static_cast<float>(atoi(tmpMeasure.c_str())), name, tmpstr);
		}
	}
	case DDEV_SmartDEN_IP_16_Relays: {//has only relays
		bool bHaveRelays = false;
		for (ii = 1; ii < results.size(); ii++)
		{
			tmpstr = stdstring_trim(results[ii]);

			if (!bHaveRelays && ((Idx = DenkoviCheckForIO(tmpstr, DAE_RELAY_DEF)) != -1))
			{
				bHaveRelays = true;
				continue;
			}

			if ((tmpName = DenkoviGetStrParameter(tmpstr, DAE_NAME_DEF)) != "") {
				name = tmpName;
				continue;
			}

			if (bHaveRelays && (Idx != -1) && ((tmpState = DenkoviGetIntParameter(tmpstr, DAE_STATE_DEF)) != -1))
			{
				std::stringstream sstr;
				sstr << "Relay " << Idx << " (" << name << ")";
				SendGeneralSwitch(DIOType_Relay, Idx, 255, (tmpState == 1) ? true : false, 100, sstr.str());
				Idx = -1;
				bHaveRelays = false;
				continue;
			}
		}
		break;
	}
	case DDEV_SmartDEN_IP_Watchdog: {//has only relays
		bool bHaveRelays = false;
		for (ii = 1; ii < results.size(); ii++)
		{
			tmpstr = stdstring_trim(results[ii]);

			if (!bHaveRelays && ((Idx = DenkoviCheckForIO(tmpstr, DAE_CH_DEF)) != -1))
			{
				bHaveRelays = true;
				continue;
			}

			if ((tmpName = DenkoviGetStrParameter(tmpstr, DAE_NAME_DEF)) != "") {
				name = tmpName;
				continue;
			}

			if (bHaveRelays && (Idx != -1) && ((tmpState = DenkoviGetIntParameter(tmpstr, DAE_RSTATE_DEF)) != -1))
			{
				name = "Relay " + std::to_string(Idx) + "(" + name + ")";
				SendGeneralSwitch(DIOType_Relay, Idx, 255, (tmpState == 1) ? true : false, 100, name);
				Idx = -1;
				bHaveRelays = false;
				continue;
			}
		}
		break;
	}
	case DDEV_SmartDEN_Notifier:
	case DDEV_SmartDEN_Logger: {
		bool bHaveDigitalInput = false;
		bool bHaveAnalogInput = false;
		bool bHaveTemperatureInput = false;
		for (ii = 1; ii < results.size(); ii++)
		{
			tmpstr = stdstring_trim(results[ii]);

			if (Idx == -1) {
				if (!bHaveDigitalInput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_DI_DEF)) != -1))
				{
					bHaveDigitalInput = true;
					continue;
				}

				if (!bHaveAnalogInput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_AI_DEF)) != -1))
				{
					bHaveAnalogInput = true;
					continue;
				}

				if (!bHaveTemperatureInput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_TI_DEF)) != -1))
				{
					bHaveTemperatureInput = true;
					continue;
				}
			}

			if ((tmpName = DenkoviGetStrParameter(tmpstr, DAE_NAME_DEF)) != "") {
				name = tmpName;
				continue;
			}

			if (bHaveDigitalInput && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_VALUE_DEF)) != -1))
			{
				name = "Digital Input " + std::to_string(Idx) + " (" + name + ")";
				SendGeneralSwitch(DIOType_DI, Idx, 255, (tmpValue == 1) ? true : false, 100, name);
				Idx = -1;
				bHaveDigitalInput = false;
				continue;
			}

			if (bHaveAnalogInput && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_VALUE_DEF)) != -1))
			{
				name = "Analog Input " + std::to_string(Idx) + " (" + name + ")";
				SendCustomSensor(Idx, 1, 255, static_cast<float>(tmpValue), name, "");
				Idx = -1;
				bHaveAnalogInput = false;
				continue;
			}

			if (bHaveTemperatureInput && (Idx != -1) && ((tmpTiValue = DenkoviGetFloatParameter(tmpstr, DAE_VALUE_DEF)) != NAN))
			{
				name = "Temperature Input " + std::to_string(Idx) + " (" + name + ")";
				SendTempSensor(Idx, 255, tmpTiValue, name);
				Idx = -1;
				bHaveTemperatureInput = false;
				continue;
			}
		}
		break;
	}
	case DDEV_SmartDEN_IP_32_In: {
		bool bHaveDigitalInput = false;
		bool bHaveAnalogInput = false;
		bool bHaveTemperatureInput = false;
		for (ii = 1; ii < results.size(); ii++)
		{
			tmpstr = stdstring_trim(results[ii]);

			if (Idx == -1) {
				if (!bHaveDigitalInput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_DI_DEF)) != -1))
				{
					bHaveDigitalInput = true;
					continue;
				}

				if (!bHaveAnalogInput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_AI_DEF)) != -1))
				{
					bHaveAnalogInput = true;
					continue;
				}

				if (!bHaveTemperatureInput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_TI_DEF)) != -1))
				{
					bHaveTemperatureInput = true;
					continue;
				}
			}

			if ((tmpName = DenkoviGetStrParameter(tmpstr, DAE_NAME_DEF)) != "") {
				name = tmpName;
				continue;
			}

			if (bHaveDigitalInput && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_VALUE_DEF)) != -1))
			{
				name = "Digital Input " + std::to_string(Idx) + " (" + name + ")";
				SendGeneralSwitch(DIOType_DI, Idx, 255, (tmpValue == 1) ? true : false, 100, name);
				Idx = -1;
				bHaveDigitalInput = false;
				continue;
			}

			if (bHaveAnalogInput && (Idx != -1) && ((tmpMeasure = DenkoviGetStrParameter(tmpstr, DAE_MEASURE_DEF)) != ""))
			{
				std::vector<std::string> vMeasure;
				StringSplit(tmpMeasure, " ", vMeasure);
				if (vMeasure.size() == 2)
				{
					name = "Analog Input " + std::to_string(Idx) + " (" + name + ")";
					SendCustomSensor(Idx, 1, 255, static_cast<float>(atof(vMeasure[0].c_str())), name, vMeasure[1]);
				}
				Idx = -1;
				bHaveAnalogInput = false;
				continue;
			}

			if (bHaveTemperatureInput && (Idx != -1) && ((tmpTiValue = DenkoviGetFloatParameter(tmpstr, DAE_VALUE_DEF)) != NAN))
			{
				name = "Temperature Input " + std::to_string(Idx) + " (" + name + ")";
				tmpMeasure = DenkoviGetStrParameter(tmpstr, DAE_VALUE_DEF);
				if ((tmpMeasure.find("?") != std::string::npos) && (tmpMeasure.find("---") == std::string::npos)) {
					tmpTiValue = (float)ConvertToCelsius((double)tmpTiValue);
				}
				SendTempSensor(Idx, 255, tmpTiValue, name);
				Idx = -1;
				bHaveTemperatureInput = false;
				continue;
			}
		}
		break;
	}
	case DDEV_SmartDEN_IP_Maxi: {//has DI, AO, AI, Relays
		bool bHaveDigitalInput = false;
		bool bHaveAnalogOutput = false;
		bool bHaveAnalogInput = false;
		bool bHaveRelay = false;
		for (ii = 1; ii < results.size(); ii++)
		{
			tmpstr = stdstring_trim(results[ii]);

			if (Idx == -1) {
				if (!bHaveDigitalInput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_DI_DEF)) != -1))
				{
					bHaveDigitalInput = true;
					continue;
				}

				if (!bHaveAnalogInput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_AI_DEF)) != -1))
				{
					bHaveAnalogInput = true;
					continue;
				}

				if (!bHaveAnalogOutput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_AO_DEF)) != -1))
				{
					bHaveAnalogOutput = true;
					continue;
				}

				if (!bHaveRelay && ((Idx = DenkoviCheckForIO(tmpstr, DAE_RELAY_DEF)) != -1))
				{
					bHaveRelay = true;
					continue;
				}
			}

			if ((tmpName = DenkoviGetStrParameter(tmpstr, DAE_NAME_DEF)) != "") {
				name = tmpName;
				continue;
			}

			if (bHaveDigitalInput && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_VALUE_DEF)) != -1))
			{
				name = "Digital Input " + std::to_string(Idx) + " (" + name + ")";
				SendGeneralSwitch(DIOType_DI, Idx, 255, (tmpValue == 1) ? true : false, 100, name);
				Idx = -1;
				bHaveDigitalInput = false;
				continue;
			}

			if (bHaveAnalogInput && (Idx != -1) && ((tmpMeasure = DenkoviGetStrParameter(tmpstr, DAE_MEASURE_DEF)) != ""))
			{
				std::vector<std::string> vMeasure;
				StringSplit(tmpMeasure, " ", vMeasure);
				if (Idx <= 4)
				{
					name = "Analog Input " + std::to_string(Idx) + " (" + name + ")";
					SendCustomSensor(Idx, 1, 255, static_cast<float>(atof(vMeasure[0].c_str())), name, vMeasure[1]);
				}
				else {
					name = "Analog Input " + std::to_string(Idx) + " (" + name + ")";
					if (vMeasure.size() == 2) {
						tmpTiValue = atof(vMeasure[0].c_str());
						if (vMeasure[1] == "degF")
							tmpTiValue = (float)ConvertToCelsius((double)tmpTiValue);
						SendTempSensor(Idx, 255, tmpTiValue, name);
					}
					else
						SendTempSensor(Idx, 255, NAN, name);
				}
				Idx = -1;
				bHaveAnalogInput = false;
				continue;
			}

			if (bHaveRelay && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_VALUE_DEF)) != -1))
			{
				name = "Relay " + std::to_string(Idx) + " (" + name + ")";
				SendGeneralSwitch(DIOType_Relay, Idx, 255, (tmpValue == 1) ? true : false, 100, name);
				Idx = -1;
				bHaveRelay = false;
				continue;
			}

			if (bHaveAnalogOutput && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_VALUE_DEF)) != -1))
			{
				name = "Analog Output " + std::to_string(Idx) + " (" + name + ")";
				double val = (100 / 1023) * tmpValue;
				SendGeneralSwitch(DIOType_AO, Idx, 255, (tmpValue > 0) ? true : false, (int)val, name);
				Idx = -1;
				bHaveAnalogOutput = false;
				continue;
			}
		}
		break;
	}
	case DDEV_DAEnet_IP4: {//has DI, DO, AI, PWM
		bool bHaveDigitalInput = false;
		bool bHaveDigitalOutput = false;
		bool bHaveAnalogInput = false;
		bool bHavePWM = false;
		for (ii = 1; ii < results.size(); ii++)
		{
			tmpstr = stdstring_trim(results[ii]);

			if (Idx == -1) {
				if (!bHaveDigitalInput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_DI_DEF)) != -1))
				{
					bHaveDigitalInput = true;
					continue;
				}

				if (!bHaveAnalogInput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_AI_DEF)) != -1))
				{
					bHaveAnalogInput = true;
					continue;
				}

				if (!bHaveDigitalOutput && ((Idx = DenkoviCheckForIO(tmpstr, DAE_OUT_DEF)) != -1))
				{
					bHaveDigitalOutput = true;
					continue;
				}

				if (!bHavePWM && ((Idx = DenkoviCheckForIO(tmpstr, DAE_PWM_DEF)) != -1))
				{
					bHavePWM = true;
					continue;
				}
			}

			if ((tmpName = DenkoviGetStrParameter(tmpstr, DAE_NAME_DEF)) != "") {
				name = tmpName;
				continue;
			}

			if (bHaveDigitalInput && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_VALUE_DEF)) != -1))
			{
				name = "Digital Input " + std::to_string(Idx) + " (" + name + ")";
				SendGeneralSwitch(DIOType_DI, Idx, 255, (tmpValue == 1) ? true : false, 100, name);
				Idx = -1;
				bHaveDigitalInput = false;
				continue;
			}

			if (bHaveAnalogInput && (Idx != -1) && ((tmpMeasure = DenkoviGetStrParameter(tmpstr, DAE_MEASURE_DEF)) != ""))
			{
				std::vector<std::string> vMeasure;
				StringSplit(tmpMeasure, " ", vMeasure);
				if (vMeasure.size() == 2)
				{
					name = "Analog Input " + std::to_string(Idx) + " (" + name + ")";
					SendCustomSensor(Idx, 1, 255, static_cast<float>(atof(vMeasure[0].c_str())), name, vMeasure[1]);
				}
				Idx = -1;
				bHaveAnalogInput = false;
				continue;
			}

			if (bHaveDigitalOutput && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_VALUE_DEF)) != -1))
			{
				name = "Digital Output " + std::to_string(Idx) + " (" + name + ")";
				SendGeneralSwitch(DIOType_DO, Idx, 255, (tmpValue == 1) ? true : false, 100, name);
				Idx = -1;
				bHaveDigitalOutput = false;
				continue;
			}

			if (bHavePWM && (Idx != -1) && ((tmpValue = DenkoviGetIntParameter(tmpstr, DAE_VALUE_DEF)) != -1))
			{
				name = "PWM " + std::to_string(Idx) + " (" + name + ")";
				SendGeneralSwitch(DIOType_PWM, Idx, 255, (tmpValue > 0) ? true : false, tmpValue, name);
				Idx = -1;
				bHavePWM = false;
				continue;
			}
		}
		break;
	}
	}
}
