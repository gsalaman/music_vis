// Music Visualizer
// 64x32 RGB Matrix.
// All freq, no time.
// Use 64 samples to do an FHT; yields 32 frequency bins
// Only display bottom 21 
// Sample audio using regular analogReads...3 KHz, interrupts enabled.

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

//  We're using A5 as our audio input pin.
#define AUDIO_PIN A5

// Gain will tell us how to scale the samples to fit in the "time" space display.
// We use this to divide the input signal, so bigger numbers make the input smaller.
int gain=10;

// We use the following define to act as a "gain stage"...adjust this up or down so
// that the FHT output fills the range for your input volume.
#define MAX_FREQ_MAG 20

// These are the raw samples from the audio input.
#define SAMPLE_SIZE FHT_N
int sample[SAMPLE_SIZE] = {0};

//  Audio samples from the ADC are "centered" around 2.5v, which maps to 512 on the ADC.
#define SAMPLE_BIAS 512

// We have half the number of frequency bins as samples.
#define FREQ_BINS (SAMPLE_SIZE/2)

// This contains the "biggest" frequency seen in a while. 
// We'll build in automatic decay.
// Note 3KHz / 16 is 187 Hz per bin.
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

// This function fills our buffer with audio samples.
void collect_samples( void )
{
  int i;
  
  for (i = 0; i < SAMPLE_SIZE; i++)
  {
    sample[i] = analogRead(AUDIO_PIN);
  }
}

// This function takes a raw sample (0-511) and maps it to a number from 0-15 for display
// on our RGB matrix.
int map_sample( int input )
{
  int mapped_sample;

  // start by taking out DC bias.  This will make negative #s...
  mapped_sample = input - SAMPLE_BIAS;

  // add in gain.
  mapped_sample = mapped_sample / gain;
  
  // center on 16.
  mapped_sample = mapped_sample + 8;

  // and clip.
  if (mapped_sample > 15) mapped_sample = 15;
  if (mapped_sample < 0) mapped_sample = 0;

  return mapped_sample;
}


// This function takes our buffer of input samples (time based) and
// displays them on our RGB matrix.
void show_samples_lines( void )
{
  int x;
  int y;
  int last_x;
  int last_y;

  // For the first column, start with the y value of that sample.
  last_x = 0;
  last_y = map_sample(sample[0]);

  // now draw the rest.
  for (x=1; x < SAMPLE_SIZE; x++)
  {
    y=map_sample(sample[x]);
    matrix.drawLine(last_x,last_y,x,y,matrix.Color333(0,0,1));
    last_x = x;
    last_y = y;
  }
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

  // Their lin mag functons corrupt memory!!!  Gonna try this for the 32 point one...we may be okay.
  fht_mag_lin();  
}
 


// This function has a little more persistent frequency display.
// If the frequency bin magnitude is currently the biggest, store it.
// If it isn't, decay the current frequency bin by 1.
void display_freq_decay( void )
{
  int i;
  int mag;
  
  int x;    


  // we have 32 freq bins, but I want to each bin to be 3 wide.
  // This means I'm going from bins 1 to 21 (which gets us to 63)
  for (i = 0; i < 21; i++)
  {
    // figure out (and map) the current frequency bin range.
    // Note we're going from 0 to -15, where -15 indicates the biggest magnitude.
    mag = fht_lin_out[i];
    mag = constrain(mag, 0, MAX_FREQ_MAG);
    mag = map(mag, 0, MAX_FREQ_MAG, 0, 31);

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

void setup() 
{
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
  display_freq_decay();

  // since we're double-buffered, this updates the display
  matrix.swapBuffers(true);

  
}
