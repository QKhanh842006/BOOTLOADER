#ifndef INC_BOOTLOADER_H_
#define INC_BOOTLOADER_H_

#include "main.h"

/* Bản đồ phân vùng bộ nhớ Flash trên STM32F411RE / STM32F411VE */
#define BOOTLOADER_ADDRESS      0x08000000U   /* Sector 0: 16 KB */
#define OTA_FLAG_ADDRESS        0x08004000U   /* Sector 1: 16 KB */
#define APP_ADDRESS             0x08008000U   /* Sector 2 -> 7: 480 KB */

/* Giới hạn bộ nhớ SRAM (128 KB) dùng để kiểm tra Stack Pointer hợp lệ */
#define SRAM_BASE_ADDR          0x20000000U
#define SRAM_SIZE_BYTES         (128U * 1024U)
#define SRAM_END_ADDR           (SRAM_BASE_ADDR + SRAM_SIZE_BYTES)

/* Giá trị cờ đánh dấu yêu cầu cập nhật OTA */
#define OTA_REQUEST_MAGIC       0xDEADBEEFU

/* Các mã byte giao thức UART với ESP32 */
#define OTA_START_BYTE          0xAAU
#define OTA_ACK                 0x79U
#define OTA_NACK                0x1FU
#define OTA_MAX_PAYLOAD         128U

/* Kiểu dữ liệu con trỏ hàm dùng để nhảy sang Reset_Handler của App */
typedef void (*pFunction)(void);

/* Nguyên mẫu các hàm xử lý của Bootloader */
uint8_t Bootloader_IsAppValid(void);
uint8_t Bootloader_IsOTARequested(void);
HAL_StatusTypeDef Bootloader_EraseAppSectors(void);
HAL_StatusTypeDef Bootloader_ClearOTAFlag(void);
void Bootloader_JumpToApp(void);
void Bootloader_ReceiveAndFlash(UART_HandleTypeDef *huart);

#endif /* INC_BOOTLOADER_H_ */
