/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "string.h"
#include <stdbool.h>
#include "OLED_1in5.h"
#include "test.h"
#include "pid.h"
#include "moving_average.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

enum Handles{T210, T245}handle;

uint8_t beep_once = false;

// 0.Create a new image cache
UBYTE *BlackImage;
UWORD Imagesize = ((OLED_1in5_WIDTH%2==0)? (OLED_1in5_WIDTH/2): (OLED_1in5_WIDTH/2+1)) * OLED_1in5_HEIGHT;
uint8_t power = 0;
uint32_t display_previousMillis = 0;
uint32_t display_Interval = 25;

uint32_t DEBUG_previousMillis = 0;
uint32_t DEBUG_Interval = 50;

uint32_t PIDupdate_previousMillis = 0;
uint32_t PIDupdate_Interval = 50;

// INITIAL TUNING PARAMETERS
volatile double Kp = 10;
volatile double Ki = 0;
volatile double Kd = 0;
volatile double custom_temp = 100;
// ----------------------------------------------

char buffer[40]; //Buffer for UART print
uint8_t UART1_rxBuffer[12] = {0};
uint8_t rx_flag = 0;
uint8_t txDone = true;
uint8_t byte, rx; //Was previously char

float powerReductionFactor = 0.12; // Converts power in W to correct duty cycle
float max_power = 0; // Sets the maximum output power

float ADC_filter_Mean = 0.0; //ADC reading value
#define ADC1_BUF_LEN 400 // takes 564 us to fill up buffer
uint16_t adc1_buf[ADC1_BUF_LEN];

float voltageCompensationConstant = 0.00648678945;

// TC Compensation constants
float TCCompensationConstant_x3_T210 = -6.798689261365103e-09;
float TCCompensationConstant_x2_T210 = -6.084684965926526e-06;
float TCCompensationConstant_x1_T210 = 0.2710175613404393;
float TCCompensationConstant_x0_T210 = 25.398999666765054; //This is the actual temp from the temp sensor, first, use 23, later the actual amb temp

float TCCompensationConstant_x3_T245 = 2.0923844111330006e-09;
float TCCompensationConstant_x2_T245 = -1.2139133328964936e-05;
float TCCompensationConstant_x1_T245 = 0.11753371673595008;
float TCCompensationConstant_x0_T245 = 25.051871505499836; //This is the actual temp from the temp sensor, first, use 23, later the actual amb temp

float TC_temperature_temp = 0.0;

float ambTempCompensationConstant = 3.3/4095;

struct sensorValues_struct {
	double set_temp;
	double act_temp;
	float busVoltage;
	float pcbTemp;
	uint8_t inStand;
	float amb_temp;
};

struct sensorValues_struct sensorValues  = {.set_temp = 0.0,
        									.act_temp = 0.0,
											.busVoltage = 0.0,
											.pcbTemp = 0.0,
											.inStand = 0,
											.amb_temp = 0.0};

double heaterPower = 0.0;
double heaterPower_DutyCycle = 0.0;

FilterTypeDef act_temp_filterStruct;
FilterTypeDef amb_temp_filterStruct;
FilterTypeDef inp_voltage_filterStruct;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
 ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim16;
TIM_HandleTypeDef htim17;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM2_Init(void);
static void MX_SPI1_Init(void);
static void MX_DMA_Init(void);
static void MX_TIM17_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM16_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


PID_TypeDef TPID;
double Temp, PIDOut, TempSetpoint;

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *UartHandle){
	/* Set transmission flag: transfer complete */
	txDone = true;
}

//Returns the average of 100 readings of the index+4*n value in the adc1_buf vector
float get_mean_ADC_reading(uint8_t index){
	ADC_filter_Mean = 0;
	for(int n=index;n<400;n=n+4){
		ADC_filter_Mean += adc1_buf[n];
	}
	return ADC_filter_Mean/100.0;
}

void get_busVoltage(){
	//Index 3 is bus Voltage
	sensorValues.busVoltage = Moving_Average_Compute(get_mean_ADC_reading(3), &inp_voltage_filterStruct)*voltageCompensationConstant;
}

void get_act_temp(){
	//Index 0 is bus Voltage
	TC_temperature_temp = Moving_Average_Compute(get_mean_ADC_reading(0), &act_temp_filterStruct);

	if(handle == T210){
		sensorValues.act_temp = pow(TC_temperature_temp, 3)*TCCompensationConstant_x3_T210 + pow(TC_temperature_temp, 2)*TCCompensationConstant_x2_T210  +  	TC_temperature_temp*TCCompensationConstant_x1_T210  +  TCCompensationConstant_x0_T210;
	}
	else if(handle == T245){
		sensorValues.act_temp = pow(TC_temperature_temp, 3)*TCCompensationConstant_x3_T245 + pow(TC_temperature_temp, 2)*TCCompensationConstant_x2_T245  +  	TC_temperature_temp*TCCompensationConstant_x1_T245  +  TCCompensationConstant_x0_T245;
	}

	if(sensorValues.act_temp > 999){
		sensorValues.act_temp = 999;
	}
}

void get_ambient_temp(){
	//Index 2 is PCB temp
	sensorValues.amb_temp = ((Moving_Average_Compute(get_mean_ADC_reading(2), &amb_temp_filterStruct)*ambTempCompensationConstant)-0.4)/0.0195;
	//• Positive slope sensor gain, offset (typical):
	//– 19.5 mV/°C, 400 mV at 0°C (TMP236-Q1) From data sheet
}

void debugPrint(UART_HandleTypeDef *huart, char _out[]){
    txDone = false;
	HAL_UART_Transmit_IT(huart, (uint8_t *) _out, strlen(_out));
	while(!txDone);
}

void init_OLED(){
	if((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
		return;
	}
	Paint_NewImage(BlackImage, OLED_1in5_WIDTH, OLED_1in5_HEIGHT, 270, BLACK);
	Paint_SetScale(16);
	Paint_SelectImage(BlackImage);
	Driver_Delay_ms(100);
	Paint_Clear(BLACK);

	// Show image
	OLED_1in5_Display(BlackImage);

	OLED_1in5_Init();
	Driver_Delay_ms(500);
	OLED_1in5_Clear();
}

void update_OLED(){
	Paint_DrawString_EN(0, 0, " AxxSolder ", &Font16, 0x00, 0xff);
    Paint_DrawLine(0, 16, 127, 16, WHITE , 2, LINE_STYLE_SOLID);

	Paint_DrawString_EN(3, 20, "Set temp", &Font16, 0x00, 0xff);
	memset(&buffer, '\0', sizeof(buffer));
	sprintf(buffer, "%.f", sensorValues.set_temp);
	Paint_DrawString_EN(3, 32, buffer, &Font24,  0x0, 0xff);
	Paint_DrawCircle(67, 37, 2, WHITE, 1, DRAW_FILL_EMPTY);
	Paint_DrawString_EN(70, 32, "C", &Font24,  0x0, 0xff);

	Paint_DrawString_EN(3, 58, "Act temp", &Font16, 0x00, 0xff);
	memset(&buffer, '\0', sizeof(buffer));

	if(sensorValues.act_temp >= 600){
		Paint_DrawString_EN(3, 70, "---", &Font24, 0x0, 0xff);
	}
	else{
		sprintf(buffer, "%.f", sensorValues.act_temp);
		Paint_DrawString_EN(3, 70, buffer, &Font24, 0x0, 0xff);
	}

	Paint_DrawCircle(67, 75, 2, WHITE, 1, DRAW_FILL_EMPTY);
	Paint_DrawString_EN(70, 70, "C", &Font24, 0x0, 0xff);
	Paint_DrawRectangle(1, 56, 100, 93 , WHITE, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);

	Paint_DrawString_EN(0, 96, "HANDLE:", &Font12, 0x00, 0xff);
	if(handle == T210){
		Paint_DrawString_EN(60, 96, "T210", &Font12, 0x00, 0xff);
	}
	else if(handle == T245){
		Paint_DrawString_EN(60, 96, "T245", &Font12, 0x00, 0xff);
	}

	Paint_DrawString_EN(0, 109, "INPUT VOLTAGE:", &Font8, 0x00, 0xff);
	Paint_DrawString_EN(0, 118, "AMB TEMP:     POWER ->", &Font8, 0x00, 0xff);

	memset(&buffer, '\0', sizeof(buffer));
	sprintf(buffer, "%.1f", sensorValues.busVoltage);
	Paint_DrawString_EN(75, 109, buffer, &Font8, 0x0, 0xff);

	memset(&buffer, '\0', sizeof(buffer));
	sprintf(buffer, "%.1f", sensorValues.amb_temp);
	Paint_DrawString_EN(45, 118, buffer, &Font8, 0x0, 0xff);

	Paint_DrawRectangle(116, 25, 128, 128, WHITE, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
	if(sensorValues.inStand){
		Paint_DrawString_EN(116, 30,  "Z", &Font16, 0x00, 0xff);
		Paint_DrawString_EN(116, 50,  "z", &Font16, 0x00, 0xff);
		Paint_DrawString_EN(116, 70,  "Z", &Font16, 0x00, 0xff);
		Paint_DrawString_EN(116, 90,  "z", &Font16, 0x00, 0xff);
		Paint_DrawString_EN(116, 110, "z", &Font16, 0x00, 0xff);

	}
	else{
		Paint_DrawRectangle(116, 125-heaterPower/10, 128, 128, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
	}
	// Show image on page2
	OLED_1in5_Display(BlackImage);
	Paint_Clear(BLACK);
}

void get_set_temp(){
	// Get encoder value (Set temp.) and limit
	if (TIM3->CNT<=20){TIM3->CNT=20;}
	if (TIM3->CNT>=450){TIM3->CNT=450;}

	sensorValues.set_temp = TIM3->CNT;
}

void get_stand_status(){
	if((HAL_GPIO_ReadPin (GPIOA, INPUT0_Pin) == 0) || (HAL_GPIO_ReadPin (GPIOA, INPUT1_Pin) == 0)){
		sensorValues.inStand = 1;
	}
	else{
		sensorValues.inStand = 0;
	}
}

void beep(int beep_time){
  	TIM2->CCR1 = 50;
  	HAL_Delay(beep_time);
  	TIM2->CCR1 = 0;
}

// Called when buffer is completely filled, used for DEBUG
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    //HAL_GPIO_TogglePin(GPIOF, DEBUG_SIGNAL_A_Pin);
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim){
	if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
		beep_once = true;
	}
}

void set_heater_duty(uint16_t dutycycle){
	TIM17->CCR1 = dutycycle;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_TIM2_Init();
  MX_SPI1_Init();
  MX_TIM17_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  MX_TIM16_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
	HAL_TIM_Encoder_Start_IT(&htim3, TIM_CHANNEL_ALL);
	HAL_TIM_PWM_Start(&htim17, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
	HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
	HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc1_buf, ADC1_BUF_LEN);//Start ADC DMA
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

	// Init and fill filter structures with initial values
	handle = T210; // Default handle
	set_heater_duty(0);
	for (int i = 0; i<40;i++){
		get_busVoltage();
		get_ambient_temp();
		get_act_temp();
		HAL_Delay(1);
	}

	// Start-up beep
	beep(10);
	HAL_Delay(100);
	beep(10);

	init_OLED();

	// if button is pressed during startup - Show SETTINGS and allow to release button, Then user can choose between T210 and T245 Handle
	if (HAL_GPIO_ReadPin (GPIOA, ENC_BUTTON_Pin) == 0){
		Paint_DrawString_EN(0, 0, " SETTINGS ", &Font16, 0x00, 0xff);
		Paint_DrawLine(0, 16, 127, 16, WHITE , 2, LINE_STYLE_SOLID);
		Paint_DrawString_EN(3, 20, "Handle:", &Font12, 0x00, 0xff);
		Paint_DrawString_EN(55, 20, "T210", &Font12, 0xff, 0x00);
		Paint_DrawString_EN(90, 20, "T245", &Font12, 0x00, 0xff);
		OLED_1in5_Display(BlackImage);
		Paint_Clear(BLACK);

		HAL_Delay(1000);

		while(HAL_GPIO_ReadPin (GPIOA, ENC_BUTTON_Pin) == 1){
			Paint_DrawString_EN(0, 0, " SETTINGS ", &Font16, 0x00, 0xff);
			Paint_DrawLine(0, 16, 127, 16, WHITE , 2, LINE_STYLE_SOLID);

			Paint_DrawString_EN(3, 20, "Handle:", &Font12, 0x00, 0xff);
			if(((TIM3->CNT)/2 % 2) == 0){
				Paint_DrawString_EN(55, 20, "T210", &Font12, 0xff, 0x00);
				Paint_DrawString_EN(90, 20, "T245", &Font12, 0x00, 0xff);
			    handle = T210;
			}
			else{
				Paint_DrawString_EN(55, 20, "T210", &Font12, 0x00, 0xff);
				Paint_DrawString_EN(90, 20, "T245", &Font12, 0xff, 0x00);
			    handle = T245;
			}
			OLED_1in5_Display(BlackImage);
			Paint_Clear(BLACK);
		}
	}

	// Set initial encoder timer value
	TIM3->CNT = 330;

	//Startup beep
	beep(10);

	// Set-up handle-specific constants
	if(handle == T210){
		max_power = 60;// 60W
		Kp = 20;
		Ki = 60;
		Kd = 0.5;
	}
	else if(handle == T245){
		max_power = 120;// 120W
		Kp = 30;
		Ki = 60;
		Kd = 1;
	}

	// Initiate PID controller
	PID(&TPID, &sensorValues.act_temp, &heaterPower, &sensorValues.set_temp, Kp, Ki, Kd, _PID_P_ON_E, _PID_CD_DIRECT);
	PID_SetMode(&TPID, _PID_MODE_AUTOMATIC);
	PID_SetSampleTime(&TPID, 50);
	PID_SetOutputLimits(&TPID, 0, 1000); // Set max and min output limit
	PID_SetILimits(&TPID, -200, 200); // Set max and min I limit

	while (1){
		// beep if encoder value is changed
		if(beep_once){
			beep(5);
			beep_once = false;
		}

		// TUNING - COMMENT OUT WHEN PID TUNING IS DONE
		// ----------------------------------------------
		//PID_SetTunings(&TPID, Kp, Ki, Kd);
		//sensorValues.set_temp = custom_temp;
		// ----------------------------------------------

		if(HAL_GetTick() - PIDupdate_previousMillis >= PIDupdate_Interval){
			get_set_temp();
			get_stand_status();
			get_busVoltage();

			set_heater_duty(0);
			HAL_Delay(10); // Wait to let the thermocouple voltage stabilize before taking measurement
			get_act_temp();
			PID_Compute(&TPID);

			if(!sensorValues.inStand){
				heaterPower_DutyCycle = heaterPower*(max_power*powerReductionFactor/sensorValues.busVoltage);
			}
			else{
				heaterPower_DutyCycle = 0;
			}
			set_heater_duty(heaterPower_DutyCycle);
			PIDupdate_previousMillis = HAL_GetTick();
		}


		if(HAL_GetTick() - DEBUG_previousMillis >= DEBUG_Interval){
			memset(&buffer, '\0', sizeof(buffer));
			sprintf(buffer, "%3.1f\t%3.1f\t%3.1f\t%3.1f\t%3.1f\t%3.1f\n", sensorValues.act_temp, sensorValues.set_temp, heaterPower/10, PID_GetPpart(&TPID)/10, PID_GetIpart(&TPID)/10, PID_GetDpart(&TPID))/10;
			debugPrint(&huart2,buffer);
			DEBUG_previousMillis = HAL_GetTick();
		}


		if(HAL_GetTick() - display_previousMillis >= display_Interval){
			get_ambient_temp();
			update_OLED();
			display_previousMillis = HAL_GetTick();
		}

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_AnalogWDGConfTypeDef AnalogWDGConfig = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.GainCompensation = 0;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.NbrOfConversion = 4;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analog WatchDog 1
  */
  AnalogWDGConfig.WatchdogNumber = ADC_ANALOGWATCHDOG_1;
  AnalogWDGConfig.WatchdogMode = ADC_ANALOGWATCHDOG_SINGLE_REG;
  AnalogWDGConfig.Channel = ADC_CHANNEL_2;
  AnalogWDGConfig.ITMode = ENABLE;
  AnalogWDGConfig.HighThreshold = 100;
  AnalogWDGConfig.LowThreshold = 0;
  AnalogWDGConfig.FilteringConfig = ADC_AWD_FILTERING_NONE;
  if (HAL_ADC_AnalogWDGConfig(&hadc1, &AnalogWDGConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_15;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x30A0A7FB;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 800-1;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 100;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 65535;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI1;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 10;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 10;
  if (HAL_TIM_Encoder_Init(&htim3, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief TIM16 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM16_Init(void)
{

  /* USER CODE BEGIN TIM16_Init 0 */

  /* USER CODE END TIM16_Init 0 */

  /* USER CODE BEGIN TIM16_Init 1 */

  /* USER CODE END TIM16_Init 1 */
  htim16.Instance = TIM16;
  htim16.Init.Prescaler = 16000-1;
  htim16.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim16.Init.Period = 10000;
  htim16.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim16.Init.RepetitionCounter = 0;
  htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim16) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM16_Init 2 */

  /* USER CODE END TIM16_Init 2 */

}

/**
  * @brief TIM17 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM17_Init(void)
{

  /* USER CODE BEGIN TIM17_Init 0 */

  /* USER CODE END TIM17_Init 0 */

  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM17_Init 1 */

  /* USER CODE END TIM17_Init 1 */
  htim17.Instance = TIM17;
  htim17.Init.Prescaler = 1600-1;
  htim17.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim17.Init.Period = 1000;
  htim17.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim17.Init.RepetitionCounter = 0;
  htim17.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim17) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim17) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim17, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim17, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM17_Init 2 */

  /* USER CODE END TIM17_Init 2 */
  HAL_TIM_MspPostInit(&htim17);

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 256000;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOF, DEBUG_SIGNAL_A_Pin|DEBUG_SIGNAL_B_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI_DC_GPIO_Port, SPI_DC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, SPI_RST_Pin|SPI_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : DEBUG_SIGNAL_A_Pin DEBUG_SIGNAL_B_Pin */
  GPIO_InitStruct.Pin = DEBUG_SIGNAL_A_Pin|DEBUG_SIGNAL_B_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pins : ENC_BUTTON_Pin INPUT0_Pin INPUT1_Pin */
  GPIO_InitStruct.Pin = ENC_BUTTON_Pin|INPUT0_Pin|INPUT1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI_DC_Pin */
  GPIO_InitStruct.Pin = SPI_DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SPI_DC_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : SPI_RST_Pin SPI_CS_Pin */
  GPIO_InitStruct.Pin = SPI_RST_Pin|SPI_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */