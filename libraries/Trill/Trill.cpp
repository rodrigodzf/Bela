#include <libraries/Trill/Trill.h>
#include <map>
#include <vector>

const uint8_t Trill::speedValues[4];
#define MAX_TOUCH_1D_OR_2D (((device_type_ == SQUARE || device_type_ == HEX) ? kMaxTouchNum2D : kMaxTouchNum1D))

static const std::map<Trill::Device, std::string> trillDeviceNameMap = {
	{Trill::NONE, "No device"},
	{Trill::UNKNOWN, "Unknown device"},
	{Trill::BAR, "Bar"},
	{Trill::SQUARE, "Square"},
	{Trill::CRAFT, "Craft"},
	{Trill::RING, "Ring"},
	{Trill::HEX, "Hex"},
};

struct trillRescaleFactors_t {
	float pos;
	float posH;
	float size;
};

static const std::vector<struct trillRescaleFactors_t> trillRescaleFactors ={
	{.pos = 1, .size = 1}, // UNKNOWN = 0,
	{.pos = 3200, .size = 4566}, // BAR = 1,
	{.pos = 1792, .posH = 1792, .size = 2700}, // SQUARE = 2,
	{.pos = 4096, .size = 1}, // CRAFT = 3,
	{.pos = 3584, .size = 5000}, // RING = 4,
	{.pos = 1920, .posH = 1664, .size = 4000}, // HEX = 5,
};

Trill::Trill(){}

Trill::Trill(unsigned int i2c_bus, uint8_t i2c_address, Mode mode) {
	setup(i2c_bus, i2c_address, mode);
}

int Trill::setup(unsigned int i2c_bus, uint8_t i2c_address, Mode mode, float threshold, int prescaler) {

	address = 0;
	if(initI2C_RW(i2c_bus, i2c_address, -1)) {
		fprintf(stderr, "Unable to initialise I2C communication\n");
		return 1;
	}

	if(identify() != 0) {
		fprintf(stderr, "Unable to identify device\n");
		return 2;
	}

	if(setMode(mode) != 0) {
		fprintf(stderr, "Unable to set mode\n");
		return 3;
	}

	if(setScanSettings(0, 12)){
		fprintf(stderr, "Unable to set scan settings\n");
		return 7;
	}

	if(threshold >= 0){
		if(setNoiseThreshold(threshold) != 0) {
			fprintf(stderr, "Unable to set threshold\n");
			return 4;
		}
	}

	if(prescaler >= 0) {
		if(setPrescaler(prescaler) != 0) {
			fprintf(stderr, "Unabe to set prescaler\n");
			return 5;
		}
	}

	if(updateBaseLine() != 0) {
		fprintf(stderr, "Unable to update baseline\n");
		return 6;
	}

	if(prepareForDataRead() != 0) {
		fprintf(stderr, "Unable to prepare for reading data\n");
		return 7;
	}

	address = i2c_address;
	return 0;
}

void Trill::cleanup() {
	closeI2C();
}

Trill::~Trill() {
	cleanup();
}

const std::string& Trill::getDeviceName()
{
	try {
		return trillDeviceNameMap.at(device_type_);
	} catch (std::exception e) {
		return trillDeviceNameMap.at(Device::UNKNOWN);
	}
}

int Trill::identify() {
	unsigned int bytesToWrite = 2;
	char buf[2] = { kOffsetCommand, kCommandIdentify };
	if(int writtenValue = (::write(i2C_file, buf, bytesToWrite)) != bytesToWrite)
	{
		fprintf(stderr, "Unexpected or no response.\n No valid device connected.\n");
		fprintf(stderr, "%d\n", writtenValue);
		return -1;
	}
	preparedForDataRead_ = false;

	usleep(commandSleepTime); // need to give enough time to process command

	::read(i2C_file, dataBuffer, 4); // discard first read

	unsigned int bytesToRead = 4;
	int bytesRead = ::read(i2C_file, dataBuffer, bytesToRead);
	if (bytesRead != bytesToRead)
	{
		fprintf(stderr, "Failure to read Byte Stream. Read %d bytes, expected %d\n", bytesRead, bytesToRead);
		device_type_ = NONE;
		return -1;
	}

	device_type_ = (Device)dataBuffer[1];
	firmware_version_ = dataBuffer[2];

	return 0;
}

void Trill::updateRescale()
{
	float scale = 1 << (12 - numBits);
	posRescale = 1.f / trillRescaleFactors[device_type_].pos;
	posHRescale = 1.f / trillRescaleFactors[device_type_].posH;
	sizeRescale = scale / trillRescaleFactors[device_type_].size;
	rawRescale = 1.f / (1 << numBits);
}

void Trill::printDetails()
{
	printf("Device type: %s (%d)\n", getDeviceName().c_str(), deviceType());
	printf("Address: %#x\n", address);
	printf("Firmware version: %d\n", firmwareVersion());
}

int Trill::setMode(Mode mode) {
	unsigned int bytesToWrite = 3;
	char buf[3] = { kOffsetCommand, kCommandMode, (char)mode };
	if(int writtenValue = (::write(i2C_file, buf, bytesToWrite)) != bytesToWrite)
	{
		fprintf(stderr, "Failed to set Trill's mode.\n");
		fprintf(stderr, "%d\n", writtenValue);
		return 1;
	}
	preparedForDataRead_ = false;
	mode_ = mode;
	usleep(commandSleepTime); // need to give enough time to process command

	return 0;
}

int Trill::setScanSettings(uint8_t speed, uint8_t num_bits) {
	unsigned int bytesToWrite = 4;
	if(speed > 3)
		speed = 3;
	if(num_bits < 9)
		num_bits = 9;
	if(num_bits > 16)
		num_bits = 16;
	char buf[4] = { kOffsetCommand, kCommandScanSettings, speed, num_bits };
	preparedForDataRead_ = false;
	if(int writtenValue = (::write(i2C_file, buf, bytesToWrite)) != bytesToWrite)
	{
		fprintf(stderr, "Failed to set Trill's scan settings.\n");
		fprintf(stderr, "%d\n", writtenValue);
		return 1;
	}
	usleep(commandSleepTime); // need to give enough time to process command
	numBits = num_bits;
	updateRescale();

	return 0;
}

int Trill::setPrescaler(uint8_t prescaler) {
	unsigned int bytesToWrite = 3;
	char buf[3] = { kOffsetCommand, kCommandPrescaler, prescaler };
	if(int writtenValue = (::write(i2C_file, buf, bytesToWrite)) != bytesToWrite)
	{
		fprintf(stderr, "Failed to set Trill's prescaler.\n");
		fprintf(stderr, "%d\n", writtenValue);
		return 1;
	}
	preparedForDataRead_ = false;
	usleep(commandSleepTime); // need to give enough time to process command

	return 0;
}

int Trill::setNoiseThreshold(float threshold) {
	unsigned int bytesToWrite = 3;
	threshold = threshold * (1 << numBits);
	if(threshold > 255)
		threshold = 255;
	if(threshold < 0)
		threshold = 0;
	char thByte = char(threshold + 0.5);
	char buf[3] = { kOffsetCommand, kCommandNoiseThreshold, thByte };
	if(int writtenValue = (::write(i2C_file, buf, bytesToWrite)) != bytesToWrite)
	{
		fprintf(stderr, "Failed to set Trill's threshold.\n");
		fprintf(stderr, "%d\n", writtenValue);
		return 1;
	}
	preparedForDataRead_ = false;
	usleep(commandSleepTime); // need to give enough time to process command

	return 0;
}


int Trill::setIDACValue(uint8_t value) {
	unsigned int bytesToWrite = 3;
	char buf[3] = { kOffsetCommand, kCommandIdac, value };
	if(int writtenValue = (::write(i2C_file, buf, bytesToWrite)) != bytesToWrite)
	{
		fprintf(stderr, "Failed to set Trill's IDAC value.\n");
		fprintf(stderr, "%d\n", writtenValue);
		return 1;
	}
	preparedForDataRead_ = false;
	usleep(commandSleepTime); // need to give enough time to process command

	return 0;
}

int Trill::setMinimumTouchSize(uint16_t size) {
	unsigned int bytesToWrite = 4;
	char buf[4] = { kOffsetCommand, kCommandMinimumSize, (char)(size >> 8), (char)(size & 0xFF) };
	if(int writtenValue = (::write(i2C_file, buf, bytesToWrite)) != bytesToWrite)
	{
		fprintf(stderr, "Failed to set Trill's minimum touch size value.\n");
		fprintf(stderr, "%d\n", writtenValue);
		return 1;
	}
	preparedForDataRead_ = false;
	usleep(commandSleepTime); // need to give enough time to process command

	return 0;
}

int Trill::setAutoScanInterval(uint16_t interval) {
	unsigned int bytesToWrite = 4;
	char buf[4] = { kOffsetCommand, kCommandAutoScanInterval, (char)(interval >> 8), (char)(interval & 0xFF) };
	if(int writtenValue = (::write(i2C_file, buf, bytesToWrite)) != bytesToWrite)
	{
		fprintf(stderr, "Failed to set Trill's auto scan interval.\n");
		fprintf(stderr, "%d\n", writtenValue);
		return 1;
	}
	preparedForDataRead_ = false;
	usleep(commandSleepTime); // need to give enough time to process command

	return 0;
}

int Trill::updateBaseLine() {
	unsigned int bytesToWrite = 2;
	char buf[2] = { kOffsetCommand, kCommandBaselineUpdate };
	if(int writtenValue = (::write(i2C_file, buf, bytesToWrite)) != bytesToWrite)
	{
		fprintf(stderr, "Failed to set Trill's baseline.\n");
		fprintf(stderr, "%d\n", writtenValue);
		return 1;
	}
	preparedForDataRead_ = false;
	usleep(commandSleepTime); // need to give enough time to process command

	return 0;
}

int Trill::prepareForDataRead() {
	unsigned int bytesToWrite = 1;
	char buf[1] = { kOffsetData };
	if(::write(i2C_file, buf, bytesToWrite) != bytesToWrite)
	{
		fprintf(stderr, "Failed to prepare Trill data collection\n");
		return 1;
	}
	preparedForDataRead_ = true;
	usleep(commandSleepTime); // need to give enough time to process command

	return 0;
}

// This should maybe be renamed readRawData()
int Trill::readI2C() {

	if(NONE == device_type_)
		return 1;

	if(!preparedForDataRead_)
		prepareForDataRead();

	int bytesRead = ::read(i2C_file, dataBuffer, kRawLength);
	if (bytesRead != kRawLength)
	{
		fprintf(stderr, "Failure to read Byte Stream. Read %d bytes, expected %d\n", bytesRead, kRawLength);
		return 1;
	}
	for (unsigned int i=0; i < getNumChannels(); i++) {
		rawData[i] = (((dataBuffer[2 * i] << 8) + dataBuffer[2 * i + 1]) & 0x0FFF) * rawRescale;
	}

	return 0;
}

int Trill::readLocations() {
	if(NONE == device_type_)
		return 1;

	if(!preparedForDataRead_)
		prepareForDataRead();

	uint8_t bytesToRead = kCentroidLengthDefault;
	if(device_type_ == SQUARE || device_type_ == HEX)
		bytesToRead = kCentroidLength2D;
	if(device_type_ == RING)
		bytesToRead = kCentroidLengthRing;
	int bytesRead = ::read(i2C_file, dataBuffer, bytesToRead);
	if (bytesRead != bytesToRead)
	{
		num_touches_ = 0;
		fprintf(stderr, "Failure to read Byte Stream. Read %d bytes, expected %d\n", bytesRead, bytesToRead);
		return 1;
	}

	unsigned int locations = 0;
	// Look for 1st instance of 0xFFFF (no touch) in the buffer
	for(locations = 0; locations < MAX_TOUCH_1D_OR_2D; locations++)
	{
		if(dataBuffer[2 * locations] == 0xFF && dataBuffer[2 * locations + 1] == 0xFF)
			break;
	}
	num_touches_ = locations;

	if(device_type_ == SQUARE || device_type_ == HEX)
	{
		// Look for the number of horizontal touches in 2D sliders
		// which might be different from number of vertical touches
		for(locations = 0; locations < MAX_TOUCH_1D_OR_2D; locations++)
		{
			if(dataBuffer[2 * locations + 4 * MAX_TOUCH_1D_OR_2D] == 0xFF
				&& dataBuffer[2 * locations + 4 * MAX_TOUCH_1D_OR_2D+ 1] == 0xFF)
				break;
		}
		num_touches_ |= (locations << 4);
	}

	return 0;
}

bool Trill::is1D()
{
	if(CENTROID != mode_)
		return false;
	switch(device_type_) {
		case BAR:
		case RING:
		case CRAFT:
			return true;
		default:
			return false;
	}
}

bool Trill::is2D()
{
	if(CENTROID != mode_)
		return false;
	switch(device_type_) {
		case SQUARE:
		case HEX:
			return true;
		default:
			return false;
	}
}

unsigned int Trill::numberOfTouches()
{
	if(mode_ != CENTROID)
		return 0;

	// Lower 4 bits hold number of 1-axis or vertical touches
	return (num_touches_ & 0x0F);
}

unsigned int Trill::numberOfHorizontalTouches()
{
	if(mode_ != CENTROID  || (device_type_ != SQUARE && device_type_ != HEX))
		return 0;

	// Upper 4 bits hold number of horizontal touches
	return (num_touches_ >> 4);
}

float Trill::touchLocation(uint8_t touch_num)
{
	if(mode_ != CENTROID)
		return -1;
	if(touch_num >= MAX_TOUCH_1D_OR_2D)
		return -1;

	int location = dataBuffer[2*touch_num] * 256;
	location += dataBuffer[2*touch_num + 1];

	return location * posRescale;
}

float Trill::getButtonValue(uint8_t button_num)
{
	if(mode_ != CENTROID)
		return -1;
	if(button_num > 1)
		return -1;
	if(device_type_ != RING)
		return -1;

	return (((dataBuffer[4*MAX_TOUCH_1D_OR_2D+2*button_num] << 8) + dataBuffer[4*MAX_TOUCH_1D_OR_2D+2*button_num+1]) & 0x0FFF) * rawRescale;
}

float Trill::touchSize(uint8_t touch_num)
{
	if(mode_ != CENTROID)
		return -1;
	if(touch_num >= MAX_TOUCH_1D_OR_2D)
		return -1;

	int size = dataBuffer[2*touch_num + 2*MAX_TOUCH_1D_OR_2D] * 256;
	size += dataBuffer[2*touch_num + 2*MAX_TOUCH_1D_OR_2D + 1];

	return size * sizeRescale;
}

float Trill::touchHorizontalLocation(uint8_t touch_num)
{
	if(mode_ != CENTROID  || (device_type_ != SQUARE && device_type_ != HEX))
		return -1;
	if(touch_num >= MAX_TOUCH_1D_OR_2D)
		return -1;

	int location = dataBuffer[2*touch_num + 4*MAX_TOUCH_1D_OR_2D] * 256;
	location += dataBuffer[2*touch_num + 4*MAX_TOUCH_1D_OR_2D+ 1];

	return location * posHRescale;
}

float Trill::touchHorizontalSize(uint8_t touch_num)
{
	if(mode_ != CENTROID  || (device_type_ != SQUARE && device_type_ != HEX))
		return -1;
	if(touch_num >= MAX_TOUCH_1D_OR_2D)
		return -1;

	int size = dataBuffer[2*touch_num + 6*MAX_TOUCH_1D_OR_2D] * 256;
	size += dataBuffer[2*touch_num + 6*MAX_TOUCH_1D_OR_2D+ 1];

	return size * sizeRescale;
}

#define compoundTouch(METHOD, TOUCHES) {\
	float avg = 0;\
	unsigned int numTouches = TOUCHES;\
	for(unsigned int i = 0; i < numTouches; i++) {\
		avg += METHOD(i);\
	}\
	if(numTouches)\
		avg = avg / numTouches;\
	return avg;\
	}

float Trill::compoundTouchLocation()
{
	compoundTouch(touchLocation, numberOfTouches());
}

float Trill::compoundTouchHorizontalLocation()
{
	compoundTouch(touchHorizontalLocation, numberOfHorizontalTouches());
}

float Trill::compoundTouchSize()
{
	compoundTouch(touchSize, is2D() ? std::min(numberOfTouches(), numberOfHorizontalTouches()) : numberOfTouches());
}

unsigned int Trill::getNumChannels()
{
	switch(device_type_) {
		case BAR: return kNumChannelsBar;
		case RING: return kNumChannelsRing;
		default: return kNumChannelsMax;
	}
}
