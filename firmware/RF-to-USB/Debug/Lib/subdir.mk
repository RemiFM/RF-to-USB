################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (12.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Lib/ssd1306.c \
../Lib/ssd1306_fonts.c 

OBJS += \
./Lib/ssd1306.o \
./Lib/ssd1306_fonts.o 

C_DEPS += \
./Lib/ssd1306.d \
./Lib/ssd1306_fonts.d 


# Each subdirectory must supply rules for building sources it contributes
Lib/%.o Lib/%.su Lib/%.cyclo: ../Lib/%.c Lib/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32L432xx -c -I../Core/Inc -I"C:/Users/remi.decoster/OneDrive - Flanders Make vzw/Documents/Personal/RF-to-USB/firmware/RF-to-USB/Lib" -I../Drivers/STM32L4xx_HAL_Driver/Inc -I../Drivers/STM32L4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Lib

clean-Lib:
	-$(RM) ./Lib/ssd1306.cyclo ./Lib/ssd1306.d ./Lib/ssd1306.o ./Lib/ssd1306.su ./Lib/ssd1306_fonts.cyclo ./Lib/ssd1306_fonts.d ./Lib/ssd1306_fonts.o ./Lib/ssd1306_fonts.su

.PHONY: clean-Lib

