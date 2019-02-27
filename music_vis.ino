// Music Visualizer
// 8x8 RGB Matrix.
// All freq, no time.
// Use 64 samples to do an FHT; yields 16 frequency bins.  We're only using the bottom 8.
// Sample audio using bit-banged ADC at 10 KHz...so 5 KHz frequncy bandwidth.  Bin size is 78 Hz/bin
// ...since I'm only displaying the bottom 8, that gives a 0 to 1.2 KHz range

// define for using the 8x8...just use fast LED.
#include <FastLED.h>

// FHT defines.  This library defines an input buffer for us called fht_input of signed integers.  
#define LIN_OUT 1
#define FHT_N  64
#include <FHT.h>

// defines for the LEDs
#define LED_PIN 7
#define LED_TYPE WS2812
#define COLOR_ORDER GRB
#define BRIGHTNESS 30

// I'm going to orient the array so that the 0th pixel is on the bottom left, 1 is one up, etc.
// The 8th pixel is the bottom most, one column to the right.
// The 63rd is the top right.
#define NUM_LEDS 64
CRGB leds[NUM_LEDS];

// 8 rows of "color" for our spec-an
CRGB matrix_color[] =
{
  CRGB::Blue,
  CRGB::Green,
  CRGB::Yellow,
  CRGB::Orange,
  CRGB::Red,
  CRGB::Purple,
  CRGB::Blue,
  CRGB::Green
};


// These are the raw samples from the audio input.
#define SAMPLE_SIZE FHT_N
int sample[SAMPLE_SIZE] = {0};

//  Audio samples from the ADC are "centered" around 2.5v, which maps to 512 on the ADC.
#define SAMPLE_BIAS 512

// We have half the number of frequency bins as samples.
#define FREQ_BINS (SAMPLE_SIZE/2)

// This contains the "biggest" frequency seen in a while. 
// We'll build in automatic decay.
int freq_hist[FREQ_BINS]={0};

void setupADC( void )
{

   // Prescalar is the last 3 bits of the ADCSRA register.  
   // Here are my measured sample rates (and resultant frequency ranges):
   // Prescalar of 128 gives ~10 KHz sample rate (5 KHz range)    mask  111
   // Prescalar of 64 gives ~20 KHz sample rate (10 KHz range)    mask: 110    
   // Prescalar of 32 gives ~40 KHz sample rate (20 KHz range)    mask: 101
   
    ADCSRA = 0b11100110;      // Upper bits set ADC to free-running mode.

    // A5, internal reference.
    ADMUX =  0b00000101;

    delay(50);  //wait for voltages to stabalize.  

}

// This function fills our buffer with audio samples.
void collect_samples( void )
{
  int i;

  //noInterrupts();
  for (i = 0; i < SAMPLE_SIZE; i++)
  {
    while(!(ADCSRA & 0x10));        // wait for ADC to complete current conversion ie ADIF bit set
    ADCSRA = ADCSRA | 0x10;        // clear ADIF bit so that ADC can do next operation (0xf5)
    sample[i] = ADC;
  }
  //interrupts();
}



// This function does the FHT to convert the time-based samples (in the sample[] array)
// to frequency bins.  The FHT library defines an array (fht_input[]) where we put our 
// input values.  After doing it's processing, fht_input will contain raw output values...
// we can use fht_lin_out() to convert those to magnitudes.
void doFHT( void )
{
  int i;
  int temp_sample;
  
  for (i=0; i < SAMPLE_SIZE; i++)
  {
    // Remove DC bias
    temp_sample = sample[i] - SAMPLE_BIAS;

    // Load the sample into the input array
    fht_input[i] = temp_sample;
    
  }
  
  fht_window();
  fht_reorder();
  fht_run();

  // Their lin mag functons corrupt memory!!! Use mine instead.
  //fht_mag_lin();  
}
 

// It looks like fht_mag_lin is corrupting memory.  Instead of debugging AVR assembly, 
// I'm gonna code my own C version.  
// I'll be using floating point math rather than assembly, so it'll be much slower...
// ...but hopefully still faster than the FFT algos.
int glenn_mag_calc(int bin)
{
  float sum_real_imag=0;
  float diff_real_imag=0;
  float result;
  int   intMag;

  // The FHT algos use the input array as it's output scratchpad.
  // Bins 0 through N/2 are the sums of the real and imaginary parts.
  // Bins N to N/2 are the differences, but note that it's reflected from the beginning.

  sum_real_imag = fht_input[bin];

  if (bin) diff_real_imag = fht_input[FHT_N - bin];

  result = (sum_real_imag * sum_real_imag) + (diff_real_imag * diff_real_imag);

  result = sqrt(result);
  result = result + 0.5;  // rounding

  intMag = result;
  
  return intMag;

}

void display_bar(int x, int y, CRGB color)
{
  int i;

  for (i=0; i<y; i++)
  {
    leds[8*x+i] = color;
  }
}

#define MAX_MAG 32
void display_freq_raw( void )
{
  int i;
  int mag;
  
  int x;    

  // only map bottom 8 bins
  for (i = 0; i < 8; i++)
  {
    // figure out (and map) the current frequency bin range.
    mag = glenn_mag_calc(i);
    mag = map(mag, 0, MAX_MAG, 0, 8);
    mag = constrain(mag, 0, 8);

    display_bar(i,mag,matrix_color[i]);
    
  }
  FastLED.show();
 
}


// This function has a little more persistent frequency display.
// If the frequency bin magnitude is currently the biggest, store it.
// If it isn't, decay the current frequency bin by 1.
#if 0
void display_freq_decay( void )
{
  int i;
  int mag;
  
  int x;    


  // we have 32 freq bins
  for (i = 0; i < 21; i++)
  {
    // figure out (and map) the current frequency bin range.
    mag = glenn_mag_calc(i);
    mag = map(mag, 0, MAX_MAG, 0, 8);
    mag = constrain(mag, 0, 8);

    // check if current magnitude is smaller than our recent history.   
    if (mag < freq_hist[i])
    {
      // decay by 1...but only if we're not going negative
      if (freq_hist[i]) 
      {
        mag = freq_hist[i] - 1;
      }
    }

    // store new value...this will either be the new max or the new "decayed" value.
    freq_hist[i] = mag;
     
    
    x = i*3;
    
    matrix.drawRect(x,32,3,0-mag, spectrum_colors[i]);
  }
 
}
#endif

void setup() 
{

  Serial.begin(9600);
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(  BRIGHTNESS );
    
  // clear the array, just in case.
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  
  setupADC();


  display_bar(0,1,CRGB::Red);
  display_bar(1,2,CRGB::Green);
  FastLED.show();

  FastLED.delay(1000);
}

void loop() 
{

  collect_samples();

  // black out anything that was there before.
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  // do the FHT to populate our frequency display
  doFHT();

  // ...and display the results.

  unsigned long start_time = micros();
  display_freq_raw();
  unsigned long stop_time = micros();

  Serial.println(stop_time - start_time);
  
}
