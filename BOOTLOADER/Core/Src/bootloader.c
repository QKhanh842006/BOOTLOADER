#include "bootloader.h"

/**
  * @brief  Kiểm tra tại địa chỉ 0x08008000 đã có User App hợp lệ hay chưa
  * @retval 1 nếu hợp lệ, 0 nếu vùng Flash đang trống hoặc sai lệch
  */
uint8_t Bootloader_IsAppValid(void)
{
    uint32_t app_msp = *(__IO uint32_t *)APP_ADDRESS;

    if ((app_msp >= SRAM_BASE_ADDR) && (app_msp <= SRAM_END_ADDR))
    {
        return 1U;
    }
    return 0U;
}

/**
  * @brief  Kiểm tra cờ yêu cầu cập nhật OTA tại Sector 1 (0x08004000)
  * @retval 1 nếu có yêu cầu OTA từ App, 0 nếu không có
  */
uint8_t Bootloader_IsOTARequested(void)
{
    uint32_t flag = *(__IO uint32_t *)OTA_FLAG_ADDRESS;

    if (flag == OTA_REQUEST_MAGIC)
    {
        return 1U;
    }
    return 0U;
}

/**
  * @brief  Xóa các Sector chứa User Application (Từ Sector 2 đến Sector 7)
  * @retval HAL_OK nếu xóa thành công
  */
HAL_StatusTypeDef Bootloader_EraseAppSectors(void)
{
    FLASH_EraseInitTypeDef EraseInitStruct = {0};
    uint32_t SectorError = 0U;
    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();

    /* Xóa toàn bộ các cờ lỗi Flash tồn đọng trước khi thao tác */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    EraseInitStruct.TypeErase    = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Sector       = FLASH_SECTOR_2;
    EraseInitStruct.NbSectors    = 6U;

    status = HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);

    HAL_FLASH_Lock();
    return status;
}

/**
  * @brief  Xóa Sector 1 (0x08004000) để gỡ bỏ cờ OTA sau khi nạp hoàn tất
  * @retval HAL_OK nếu xóa thành công
  */
HAL_StatusTypeDef Bootloader_ClearOTAFlag(void)
{
    FLASH_EraseInitTypeDef EraseInitStruct = {0};
    uint32_t SectorError = 0U;
    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    EraseInitStruct.TypeErase    = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Sector       = FLASH_SECTOR_1;
    EraseInitStruct.NbSectors    = 1U;

    status = HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);

    HAL_FLASH_Lock();
    return status;
}

/**
  * @brief  Giải phóng ngoại vi và chuyển quyền điều khiển CPU sang User Application
  */
void Bootloader_JumpToApp(void)
{
    if (Bootloader_IsAppValid() == 0U)
    {
        return;
    }

    uint32_t app_msp = *(__IO uint32_t *)APP_ADDRESS;
    uint32_t app_reset_addr = *(__IO uint32_t *)(APP_ADDRESS + 4U);
    pFunction Jump_To_Application = (pFunction)app_reset_addr;

    /* 1. Khóa toàn bộ ngắt trước khi dọn dẹp hệ thống */
    __disable_irq();

    /* 2. Dừng bộ đếm SysTick */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    /* 3. Đưa Clock và các ngoại vi về trạng thái Reset mặc định */
    HAL_RCC_DeInit();
    HAL_DeInit();

    /* 4. Xóa toàn bộ cờ cho phép ngắt và cờ ngắt đang treo trong NVIC */
    for (uint8_t i = 0U; i < 8U; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    /* 5. Dời bảng Vector ngắt sang địa chỉ bắt đầu của Application */
    SCB->VTOR = APP_ADDRESS;

    /* 6. Cập nhật thanh ghi Main Stack Pointer (MSP) cho Application */
    __set_MSP(app_msp);

    /* 7. Mở lại ngắt toàn cục và nhảy vào Reset_Handler của Application */
    __enable_irq();
    Jump_To_Application();
}

/**
  * @brief  Tiến trình xóa Flash, nhận từng gói dữ liệu từ ESP32 và ghi vào Flash
  * @param  huart: Con trỏ tới cấu trúc quản lý UART (ví dụ &huart2)
  */
void Bootloader_ReceiveAndFlash(UART_HandleTypeDef *huart)
{
    uint8_t header[2];
    uint8_t payload[OTA_MAX_PAYLOAD];
    uint8_t rx_checksum = 0U;
    uint8_t resp = 0U;
    uint32_t current_flash_addr = APP_ADDRESS;

    /* Bật LED Cam (PD13) sáng đứng báo hiệu bắt đầu xóa Flash */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_RESET);

    /* Bước 1: Xóa sạch Sector 2 đến Sector 7 (mất khoảng 2 đến 4 giây) */
    if (Bootloader_EraseAppSectors() != HAL_OK)
    {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET); /* Bật LED Đỏ báo lỗi */
        resp = OTA_NACK;
        HAL_UART_Transmit(huart, &resp, 1U, 100U);
        return;
    }

    /* Bước 2: Gửi byte ACK đầu tiên báo cho ESP32 biết đã xóa Flash xong */
    resp = OTA_ACK;
    HAL_UART_Transmit(huart, &resp, 1U, 100U);

    /* Bước 3: Vòng lặp nhận từng gói dữ liệu 128 byte */
    while (1)
    {
        /* Chờ nhận 2 byte Header: [0xAA] [Length] với timeout 10 giây */
        if (HAL_UART_Receive(huart, header, 2U, 10000U) != HAL_OK)
        {
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET);
            break;
        }

        /* Nếu byte đầu không phải 0xAA thì bỏ qua để đồng bộ lại khung truyền */
        if (header[0] != OTA_START_BYTE)
        {
            continue;
        }

        uint8_t len = header[1];

        /* Kiểm tra gói tin kết thúc (Length == 0) */
        if (len == 0U)
        {
            /* Đã nạp xong 100%, tiến hành xóa cờ OTA ở Sector 1 */
            Bootloader_ClearOTAFlag();

            /* Gửi ACK cuối cùng cho ESP32 và tắt LED Cam */
            resp = OTA_ACK;
            HAL_UART_Transmit(huart, &resp, 1U, 100U);
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);

            HAL_Delay(50U);
            Bootloader_JumpToApp();
            break;
        }

        /* Bảo vệ tràn mảng đệm nếu độ dài vượt quá 128 byte */
        if (len > OTA_MAX_PAYLOAD)
        {
            resp = OTA_NACK;
            HAL_UART_Transmit(huart, &resp, 1U, 100U);
            continue;
        }

        /* Nhận mảng dữ liệu Payload và 1 byte Checksum */
        if (HAL_UART_Receive(huart, payload, len, 2000U) != HAL_OK)
        {
            resp = OTA_NACK;
            HAL_UART_Transmit(huart, &resp, 1U, 100U);
            continue;
        }

        if (HAL_UART_Receive(huart, &rx_checksum, 1U, 2000U) != HAL_OK)
        {
            resp = OTA_NACK;
            HAL_UART_Transmit(huart, &resp, 1U, 100U);
            continue;
        }

        /* Tính toán Checksum XOR để đối chiếu */
        uint8_t calc_checksum = 0U;
        for (uint8_t i = 0U; i < len; i++)
        {
            calc_checksum ^= payload[i];
        }

        if (calc_checksum != rx_checksum)
        {
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET);
            resp = OTA_NACK;
            HAL_UART_Transmit(huart, &resp, 1U, 100U);
            continue;
        }

        /* Dữ liệu chuẩn xác -> Mở khóa Flash và ghi từng Word (4 byte) */
        HAL_FLASH_Unlock();
        HAL_StatusTypeDef flash_status = HAL_OK;

        for (uint8_t i = 0U; i < len; i += 4U)
        {
            uint32_t word_data = *(uint32_t *)(&payload[i]);
            flash_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, current_flash_addr, word_data);
            if (flash_status != HAL_OK)
            {
                break;
            }
            current_flash_addr += 4U;
        }

        HAL_FLASH_Lock();

        if (flash_status != HAL_OK)
        {
            HAL_GPIO_WritePin(GPIOD, GPIO_PIN_14, GPIO_PIN_SET);
            resp = OTA_NACK;
            HAL_UART_Transmit(huart, &resp, 1U, 100U);
            break;
        }

        /* Đảo trạng thái LED Cam (PD13) mỗi khi ghi xong 1 gói tin */
        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13);

        /* Gửi ACK yêu cầu ESP32 truyền gói tiếp theo */
        resp = OTA_ACK;
        HAL_UART_Transmit(huart, &resp, 1U, 100U);
    }
}
