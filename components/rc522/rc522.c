// Mifare RC522 RFID Card reader 13.56 MHz

#include "rc522.h"

static const char *TAG = "RC522";

void MFRC522_Init(void) {
	ESP_LOGI(TAG, "Init spi");
	
	spi_config_t spi_config;
    // Load default interface parameters
    // CS_EN:1, MISO_EN:1, MOSI_EN:1, BYTE_TX_ORDER:1, BYTE_TX_ORDER:1, BIT_RX_ORDER:0, BIT_TX_ORDER:0, CPHA:0, CPOL:0
    spi_config.interface.val = SPI_DEFAULT_INTERFACE;
	//spi_config.interface.byte_tx_order = 1;
	//spi_config.interface.byte_rx_order = 1;

    // Load default interrupt enable
    // TRANS_DONE: true, WRITE_STATUS: false, READ_STATUS: false, WRITE_BUFFER: false, READ_BUFFER: false
    spi_config.intr_enable.val = SPI_MASTER_DEFAULT_INTR_ENABLE;
    // Set SPI to master mode
    // ESP8266 Only support half-duplex
    spi_config.mode = SPI_MASTER_MODE;
    // Set the SPI clock frequency division factor
    spi_config.clk_div = SPI_10MHz_DIV;
    spi_config.event_cb = NULL;
    spi_init(HSPI_HOST, &spi_config);
	
	//gpio_set_level(MFRC522_CS_PIN, 1);
	
	ESP_LOGI(TAG, "Init rc522");
	MFRC522_Reset();
	MFRC522_WriteRegister(MFRC522_REG_T_MODE, 0x80);
	/* MFRC522_WriteRegister(MFRC522_REG_T_PRESCALER, 0x3E); */
	/* MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_L, 30);            */
	/* MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_H, 0); */
	MFRC522_WriteRegister(MFRC522_REG_T_PRESCALER, 0xA9);
	MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_L, 0xE8);           
	MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_H, 0x03);
	//MFRC522_WriteRegister(MFRC522_REG_RF_CFG, 0x70);			// 48dB gain	
	MFRC522_WriteRegister(MFRC522_REG_TX_AUTO, 0x40);
	MFRC522_WriteRegister(MFRC522_REG_MODE, 0x3D);
	MFRC522_AntennaOn();										// Open the antenna
	MFRC522_AntennaOn();
	MFRC522_AntennaOn();
}

uint8_t MFRC522_Check(uint8_t * id) {
	uint8_t status;
	status = MFRC522_Request(PICC_REQIDL, id);					// Find cards, return card type
	if (status == MI_OK) status = MFRC522_Anticoll(id);			// Card detected. Anti-collision, return card serial number 4 bytes
	MFRC522_Halt();												// Command card into hibernation 
	return status;
}

uint8_t MFRC522_Compare(uint8_t * CardID, uint8_t * CompareID) {
	uint8_t i;
	for (i = 0; i < 5; i++) {
		if (CardID[i] != CompareID[i]) return MI_ERR;
	}
	return MI_OK;
}

void MFRC522_WriteRegister(uint32_t addr, uint32_t val) {
	addr = (addr << 1);
	// Address format: 0XXXXXX0
	
	// Send address in cmd phase and data in mosi
	spi_trans_t trans;
    memset(&trans, 0x0, sizeof(trans));
    trans.bits.mosi = 8;
    trans.bits.cmd = 8;
	trans.mosi = &val;
	trans.cmd = &addr;
    spi_trans(HSPI_HOST, &trans);
	
	ESP_LOGD(TAG, "Writing %0x = %0x", addr >> 1, val);
}

uint32_t MFRC522_ReadRegister(uint32_t o_addr) {
	uint32_t val = 0;
	uint32_t addr = (o_addr << 1) | 0x80;

	// Send address in cmd phase and receive data in miso
	spi_trans_t trans;
    memset(&trans, 0x0, sizeof(trans));
	trans.miso = &val;
    trans.cmd = &addr; 
    trans.bits.miso = 8;
	trans.bits.cmd = 8;
    spi_trans(HSPI_HOST, &trans);
	
	ESP_LOGD(TAG, "Reading %0x = %0x", o_addr, val);
	
	return val;
}

void MFRC522_SetBitMask(uint8_t reg, uint8_t mask) {
	MFRC522_WriteRegister(reg, MFRC522_ReadRegister(reg) | mask);
}

void MFRC522_ClearBitMask(uint8_t reg, uint8_t mask){
	MFRC522_WriteRegister(reg, MFRC522_ReadRegister(reg) & (~mask));
} 

void MFRC522_AntennaOn(void) {
	uint8_t temp;

	temp = (uint8_t)MFRC522_ReadRegister(MFRC522_REG_TX_CONTROL);
	if (!(temp & 0x03)) { ESP_LOGI(TAG, "Antena enabling."); MFRC522_SetBitMask(MFRC522_REG_TX_CONTROL, 0x03); }
}

void MFRC522_AntennaOff(void) {
	MFRC522_ClearBitMask(MFRC522_REG_TX_CONTROL, 0x03);
	ESP_LOGI(TAG, "Antena disabled.");
}

void MFRC522_Reset(void) {
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_RESETPHASE);
}

uint8_t MFRC522_Request(uint8_t reqMode, uint8_t * TagType) {
	uint8_t status;  
	uint16_t backBits;																				// The received data bits

	MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x07);											// TxLastBists = BitFramingReg[2..0]
	TagType[0] = reqMode;
	status = MFRC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits);
	if ((status != MI_OK) || (backBits != 0x10)) status = MI_ERR;
	return status;
}

uint8_t MFRC522_ToCard(uint8_t command, uint8_t * sendData, uint8_t sendLen, uint8_t * backData, uint16_t * backLen) {
	uint8_t status = MI_ERR;
	uint8_t irqEn = 0x00;
	uint8_t waitIRq = 0x00;
	uint8_t lastBits;
	uint8_t n;
	uint16_t i;

	switch (command) {
		case PCD_AUTHENT: {
			irqEn = 0x12;
			waitIRq = 0x10;
			break;
		}
		case PCD_TRANSCEIVE: {
			irqEn = 0x77;
			waitIRq = 0x30;
			break;
		}
		default:
		break;
	}

	MFRC522_WriteRegister(MFRC522_REG_COMM_IE_N, irqEn | 0x80);
	MFRC522_ClearBitMask(MFRC522_REG_COMM_IRQ, 0x80);
	MFRC522_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_IDLE);

	// Writing data to the FIFO
	for (i = 0; i < sendLen; i++) MFRC522_WriteRegister(MFRC522_REG_FIFO_DATA, sendData[i]);

	// Execute the command
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, command);
	if (command == PCD_TRANSCEIVE) MFRC522_SetBitMask(MFRC522_REG_BIT_FRAMING, 0x80);					// StartSend=1,transmission of data starts 

	// Waiting to receive data to complete
	i = 2000;	// i according to the clock frequency adjustment, the operator M1 card maximum waiting time 25ms
	do {
		// CommIrqReg[7..0]
		// Set1 TxIRq RxIRq IdleIRq HiAlerIRq LoAlertIRq ErrIRq TimerIRq
		n = MFRC522_ReadRegister(MFRC522_REG_COMM_IRQ);
		i--;
	} while ((i!=0) && !(n&0x01) && !(n&waitIRq));

	MFRC522_ClearBitMask(MFRC522_REG_BIT_FRAMING, 0x80);												// StartSend=0

	if (i != 0)  {
		if (!(MFRC522_ReadRegister(MFRC522_REG_ERROR) & 0x1B)) {
			status = MI_OK;
			if (n & irqEn & 0x01) status = MI_NOTAGERR;
			if (command == PCD_TRANSCEIVE) {
				n = MFRC522_ReadRegister(MFRC522_REG_FIFO_LEVEL);
				lastBits = MFRC522_ReadRegister(MFRC522_REG_CONTROL) & 0x07;
				if (lastBits) *backLen = (n - 1) * 8 + lastBits; else *backLen = n * 8;
				if (n == 0) n = 1;
				if (n > MFRC522_MAX_LEN) n = MFRC522_MAX_LEN;
				for (i = 0; i < n; i++) backData[i] = MFRC522_ReadRegister(MFRC522_REG_FIFO_DATA);		// Reading the received data in FIFO
			}
		} else status = MI_ERR;
	}
	return status;
}

uint8_t MFRC522_Anticoll(uint8_t * serNum) {
	uint8_t status;
	uint8_t i;
	uint8_t serNumCheck = 0;
	uint16_t unLen;

	MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x00);												// TxLastBists = BitFramingReg[2..0]
	serNum[0] = PICC_ANTICOLL;
	serNum[1] = 0x20;
	status = MFRC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);
	if (status == MI_OK) {
		// Check card serial number
		for (i = 0; i < 4; i++) serNumCheck ^= serNum[i];
		if (serNumCheck != serNum[i]) status = MI_ERR;
	}
	return status;
} 

void MFRC522_CalculateCRC(uint8_t *  pIndata, uint8_t len, uint8_t * pOutData) {
	uint8_t i, n;

	MFRC522_ClearBitMask(MFRC522_REG_DIV_IRQ, 0x04);													// CRCIrq = 0
	MFRC522_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);													// Clear the FIFO pointer
	// Write_MFRC522(CommandReg, PCD_IDLE);

	// Writing data to the FIFO	
	for (i = 0; i < len; i++) MFRC522_WriteRegister(MFRC522_REG_FIFO_DATA, *(pIndata+i));
	MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_CALCCRC);

	// Wait CRC calculation is complete
	i = 0xFF;
	do {
		n = MFRC522_ReadRegister(MFRC522_REG_DIV_IRQ);
		i--;
	} while ((i!=0) && !(n&0x04));																		// CRCIrq = 1

	// Read CRC calculation result
	pOutData[0] = MFRC522_ReadRegister(MFRC522_REG_CRC_RESULT_L);
	pOutData[1] = MFRC522_ReadRegister(MFRC522_REG_CRC_RESULT_M);
}

uint8_t MFRC522_SelectTag(uint8_t * serNum) {
	uint8_t i;
	uint8_t status;
	uint8_t size;
	uint16_t recvBits;
	uint8_t buffer[9]; 

	buffer[0] = PICC_SElECTTAG;
	buffer[1] = 0x70;
	for (i = 0; i < 5; i++) buffer[i+2] = *(serNum+i);
	MFRC522_CalculateCRC(buffer, 7, &buffer[7]);		//??
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buffer, 9, buffer, &recvBits);
	if ((status == MI_OK) && (recvBits == 0x18)) size = buffer[0]; else size = 0;
	return size;
}

uint8_t MFRC522_Auth(uint8_t authMode, uint8_t BlockAddr, uint8_t * Sectorkey, uint8_t * serNum) {
	uint8_t status;
	uint16_t recvBits;
	uint8_t i;
	uint8_t buff[12]; 

	// Verify the command block address + sector + password + card serial number
	buff[0] = authMode;
	buff[1] = BlockAddr;
	for (i = 0; i < 6; i++) buff[i+2] = *(Sectorkey+i);
	for (i=0; i<4; i++) buff[i+8] = *(serNum+i);
	status = MFRC522_ToCard(PCD_AUTHENT, buff, 12, buff, &recvBits);
	if ((status != MI_OK) || (!(MFRC522_ReadRegister(MFRC522_REG_STATUS2) & 0x08))) status = MI_ERR;
	return status;
}

uint8_t MFRC522_Read(uint8_t blockAddr, uint8_t * recvData) {
	uint8_t status;
	uint16_t unLen;

	recvData[0] = PICC_READ;
	recvData[1] = blockAddr;
	MFRC522_CalculateCRC(recvData,2, &recvData[2]);
	status = MFRC522_ToCard(PCD_TRANSCEIVE, recvData, 4, recvData, &unLen);
	if ((status != MI_OK) || (unLen != 0x90)) status = MI_ERR;
	return status;
}

uint8_t MFRC522_Write(uint8_t blockAddr, uint8_t * writeData) {
	uint8_t status;
	uint16_t recvBits;
	uint8_t i;
	uint8_t buff[18]; 

	buff[0] = PICC_WRITE;
	buff[1] = blockAddr;
	MFRC522_CalculateCRC(buff, 2, &buff[2]);
	status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);
	if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) status = MI_ERR;
	if (status == MI_OK) {
		// Data to the FIFO write 16Byte
		for (i = 0; i < 16; i++) buff[i] = *(writeData+i);
		MFRC522_CalculateCRC(buff, 16, &buff[16]);
		status = MFRC522_ToCard(PCD_TRANSCEIVE, buff, 18, buff, &recvBits);
		if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) status = MI_ERR;
	}
	return status;
}

void MFRC522_Halt(void) {
	uint16_t unLen;
	uint8_t buff[4]; 

	buff[0] = PICC_HALT;
	buff[1] = 0;
	MFRC522_CalculateCRC(buff, 2, &buff[2]);
	MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &unLen);
}
