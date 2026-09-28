// Audio acquisition from a MAX9814 electret microphone amplifier.
//
//   MAX9814 breakout    STM32G030F6P6
//   OUT --------------> PA0 (pin 7) = ADC_IN0
//   VDD --------------- 3.3 V (same rail as the MCU)
//   GND --------------- GND (short lead, next to the MCU ground)
//   GAIN              - unconnected = 60 dB, GND = 50 dB, VDD = 40 dB
//   AR                - unconnected = attack/release 1:4000
//
// MICOUT sits on a ~1.25 V DC bias and the AGC regulates it to ~1.4 Vpp, so
// the signal stays inside the 0..3.3 V ADC range. A first-order RC low-pass
// (e.g. 1 kOhm + 47 nF, fc ~3.4 kHz) between OUT and PA0 is recommended as an
// anti-alias filter; the on-chip oversampling below only helps a little.
//
// Pipeline (no CPU involvement, no interrupts):
//   TIM3 update @ 64 kHz -> TRGO -> ADC1 conversion (triggered oversampling,
//   one conversion per trigger, 8 conversions averaged = 8 kHz, 12 bit)
//   -> DMA1 channel 1, circular, into a 2 x kBlock ping-pong buffer.
// The application polls ready_block() for the half the DMA just finished.
//
// Peripherals are programmed directly through the CMSIS register definitions
// (CubeMX has no ADC/TIM configured and did not copy the LL ADC/TIM headers).
module;
#include "stm32g0xx_hal.h"
#include "stm32g0xx_ll_dma.h"
export module audio_adc;
import std;

export namespace audio_adc {

inline constexpr std::uint32_t kSampleRateHz = 8000;
inline constexpr std::size_t   kBlock        = 256;  // samples per ping-pong half

using Block = std::span<const std::uint16_t, kBlock>;

class AudioAdc {
public:
    constexpr AudioAdc() = default;

    // Configures PA0, TIM3, ADC1 and DMA1 channel 1 and starts sampling.
    void init();

    // The half of the ring buffer the DMA completed since the last call, or
    // nullopt if none. The data stays valid for one block period (32 ms);
    // copy it out before then.
    std::optional<Block> ready_block();

    // Blocks lost because ready_block() was not called in time.
    std::uint32_t overruns() const { return overruns_; }

private:
    std::array<std::uint16_t, 2 * kBlock> buf_{};
    std::uint32_t overruns_ = 0;
};

}  // namespace audio_adc

namespace audio_adc {

namespace {

constexpr std::uint32_t kTimerClockHz = 16'000'000;  // PCLK, SYSCLK = HSI16
constexpr std::uint32_t kOversample   = 8;           // OVSR = 010, OVSS = 0011
constexpr std::uint32_t kTriggerHz    = kSampleRateHz * kOversample;
static_assert(kTimerClockHz % kTriggerHz == 0);

}  // namespace

void AudioAdc::init() {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    GPIO_InitTypeDef gpio{};
    gpio.Pin  = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    // --- ADC1 -------------------------------------------------------------
    // Clock PCLK/2 = 8 MHz (CKMODE may only change while ADEN = 0).
    // Oversampling x8, shift 3 -> 12-bit average; TOVS: one conversion per
    // trigger, so the 8 samples are spread over one 125 us output period and
    // act as a boxcar low-pass instead of a burst.
    ADC1->CFGR2 = ADC_CFGR2_CKMODE_0
                | ADC_CFGR2_OVSE
                | ADC_CFGR2_OVSR_1                       // 010 = x8
                | ADC_CFGR2_OVSS_1 | ADC_CFGR2_OVSS_0    // 0011 = >> 3
                | ADC_CFGR2_TOVS;

    ADC1->CR = ADC_CR_ADVREGEN;
    HAL_Delay(1);  // tADCVREG_STUP = 20 us

    ADC1->CR |= ADC_CR_ADCAL;  // DMAEN must be 0 during calibration
    while (ADC1->CR & ADC_CR_ADCAL) {
    }

    // External trigger TIM3_TRGO (EXTSEL = 011) on the rising edge, 12 bit,
    // right aligned, circular DMA. OVRMOD: overwrite DR on overrun instead of
    // stopping conversions, so a late DMA can never stall the stream.
    ADC1->CFGR1 = ADC_CFGR1_EXTSEL_1 | ADC_CFGR1_EXTSEL_0
                | ADC_CFGR1_EXTEN_0
                | ADC_CFGR1_OVRMOD
                | ADC_CFGR1_DMACFG | ADC_CFGR1_DMAEN;
    ADC1->SMPR = ADC_SMPR_SMP1_2 | ADC_SMPR_SMP1_0;  // 101 = 39.5 cycles, ~6.5 us/conversion

    ADC1->ISR = ADC_ISR_CCRDY;
    ADC1->CHSELR = ADC_CHSELR_CHSEL0;
    while (!(ADC1->ISR & ADC_ISR_CCRDY)) {
    }

    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    while (!(ADC1->ISR & ADC_ISR_ADRDY)) {
    }

    // --- DMA1 channel 1 <- ADC1 -------------------------------------------
    LL_DMA_ConfigTransfer(DMA1, LL_DMA_CHANNEL_1,
                          LL_DMA_DIRECTION_PERIPH_TO_MEMORY | LL_DMA_MODE_CIRCULAR |
                          LL_DMA_PERIPH_NOINCREMENT | LL_DMA_MEMORY_INCREMENT |
                          LL_DMA_PDATAALIGN_HALFWORD | LL_DMA_MDATAALIGN_HALFWORD |
                          LL_DMA_PRIORITY_HIGH);
    LL_DMA_SetPeriphRequest(DMA1, LL_DMA_CHANNEL_1, LL_DMAMUX_REQ_ADC1);
    LL_DMA_SetPeriphAddress(DMA1, LL_DMA_CHANNEL_1, reinterpret_cast<std::uint32_t>(&ADC1->DR));
    LL_DMA_SetMemoryAddress(DMA1, LL_DMA_CHANNEL_1, reinterpret_cast<std::uint32_t>(buf_.data()));
    LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_1, buf_.size());
    LL_DMA_ClearFlag_GI1(DMA1);
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_1);

    ADC1->CR |= ADC_CR_ADSTART;  // armed, waits for the first trigger

    // --- TIM3: update event -> TRGO at kTriggerHz --------------------------
    TIM3->PSC = 0;
    TIM3->ARR = kTimerClockHz / kTriggerHz - 1;
    TIM3->CR2 = TIM_CR2_MMS_1;  // MMS = 010: update
    TIM3->CR1 = TIM_CR1_CEN;
}

std::optional<Block> AudioAdc::ready_block() {
    const bool half = LL_DMA_IsActiveFlag_HT1(DMA1);
    const bool full = LL_DMA_IsActiveFlag_TC1(DMA1);
    if (!half && !full) {
        return std::nullopt;
    }

    // Both flags pending: a whole block went by unread. The second half is
    // the more recent one only if TC came last, but either way we are late.
    if (half && full) {
        ++overruns_;
    }
    if (half) {
        LL_DMA_ClearFlag_HT1(DMA1);
    }
    if (full) {
        LL_DMA_ClearFlag_TC1(DMA1);
    }

    // The DMA wrote the buffer behind the compiler's back.
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const std::uint16_t* first = full ? buf_.data() + kBlock : buf_.data();
    return Block{first, kBlock};
}

}  // namespace audio_adc
