// Music Visualizer
// 64x32 RGB Matrix.
// All freq, no time.
// Use 64 samples to do an FHT; yields 32 frequency bins.  We're only using the bottom 21.
// Sample audio using bit-banged ADC at 20 KHz...so 10 KHz frequncy bandwidth, but only displaying up to ~6.67 KHz. 

// These two defines are for the RGB Matrix
#include <Adafruit_GFX.h>   // Core graphics library
#include <RGBmatrixPanel.h> // Hardware-specific library

// FHT defines.  This library defines an input buffer for us called fht_input of signed integers.  
#define LIN_OUT 1
#define FHT_N   64
#include <FHT.h>

// Pin defines for the 32x32 RGB matrix.
#define CLK 11  
#define LAT 10
#define OE  9
#define A   A0
#define B   A1
#define C   A2
#define D   A3

/* Other Pin Mappings...hidden in the RGB library:
 *  Sig   Uno  Mega
 *  R0    2    24
 *  G0    3    25
 *  B0    4    26
 *  R1    5    27
 *  G1    6    28
 *  B1    7    29
 */

// Note "false" for double-buffering to consume less memory, or "true" for double-buffered.
// Double-buffered makes updates look smoother.
RGBmatrixPanel matrix(A, B, C,  D,  CLK, LAT, OE, true, 64);

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

// Color pallete for spectrum...cooler than just single green.
uint16_t spectrum_colors[] = 
{
  matrix.Color444(7,0,0),   // index 0
  matrix.Color444(6,1,0),   // index 1
  matrix.Color444(5,2,0),   // index 2
  matrix.Color444(4,3,0),   // index 3
  matrix.Color444(3,4,0),   // index 4
  matrix.Color444(2,5,0),   // index 5
  matrix.Color444(1,6,0),   // index 6
  matrix.Color444(0,7,0),   // index 7 
  matrix.Color444(0,6,1),   // index 8
  matrix.Color444(0,5,2),   // index 9
  matrix.Color444(0,4,3),   // index 10
  matrix.Color444(0,3,4),   // index 11
  matrix.Color444(0,2,5),   // index 12 
  matrix.Color444(0,1,6),   // index 13
  matrix.Color444(0,0,7),   // index 14
  matrix.Color444(1,0,6),   // index 15
  matrix.Color444(2,0,5),   // index 16
  matrix.Color444(3,0,4),   // index 17
  matrix.Color444(4,0,3),   // index 18
  matrix.Color444(5,0,2),   // index 19
  matrix.Color444(6,0,1),   // index 20
  matrix.Color444(7,0,0),   // index 21
  
};

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

void display_freq_raw( void )
{
  int i;
  int mag;
  
  int x;    

  // only map 21 bins to give a cooler display.
  for (i = 0; i < 21; i++)
  {
    // figure out (and map) the current frequency bin range.
    mag = glenn_mag_calc(i);
    mag = constrain(mag, 0, 31);
    
    x = i*3;
    
    matrix.drawRect(x,32,3,0-mag, spectrum_colors[i%22]);
  }
 
}

// This function has a little more persistent frequency display.
// If the frequency bin magnitude is currently the biggest, store it.
// If it isn't, decay the current frequency bin by 1.
void display_freq_decay( void )
{
  int i;
  int mag;
  
  int x;    


  // we have 32 freq bins
  for (i = 0; i < 32; i++)
  {
    // figure out (and map) the current frequency bin range.
    mag = glenn_mag_calc(i);
    mag = constrain(mag, 0, 31);

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
     
    
    x = i*2;
    
    matrix.drawRect(x,32,2,0-mag, matrix.Color333(1,0,0));
  }
 
}

void setup() 
{

  Serial.begin(9600);
  
  setupADC();
  
  matrix.begin();
}

void loop() 
{

  collect_samples();

  // black out anything that was there before.
  matrix.fillScreen(0);

  // do the FHT to populate our frequency display
  doFHT();

  // ...and display the results.
  display_freq_raw();

  // since we're double-buffered, this updates the display
  matrix.swapBuffers(true);

  
}
