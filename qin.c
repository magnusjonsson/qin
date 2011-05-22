#include <lv2.h>
#include "event-helpers.h"
#include "uri-map.h"
#include <malloc.h>
#include <math.h>

#define MIDI_COMMANDMASK 0xF0
#define MIDI_CHANNELMASK 0x0F

#define MIDI_NOTEON 0x90
#define MIDI_NOTEOFF 0x80
#define MIDI_CONTROL 0xB0

enum ports {
  PORT_OUTPUT=0,
  PORT_MIDI,
  PORT_AMP_DECAY,
  PORT_LPFILTER_DECAY,
  PORT_HPFILTER_DECAY,
  PORT_AMP_RELEASE,
  PORT_LPFILTER_RELEASE,
  PORT_HPFILTER_RELEASE,
  PORT_PULSE_WIDTH,
  PORT_DETUNE,
};

struct synth_t {
  double dt; // 1/sample_rate
  float* output_p;
  LV2_Event_Buffer *midi_in_p;
  LV2_Event_Iterator midi_in_iterator;

  LV2_Event_Feature* event_feature;
  uint32_t midi_event_id;

  float const* amp_decay_p;
  float const* lpfilter_decay_p;
  float const* hpfilter_decay_p;
  float const* amp_release_p;
  float const* lpfilter_release_p;
  float const* hpfilter_release_p;
  float const* pulse_width_p;
  float const* detune_p;

  int32_t num_notes_on;

  double freq;
  double phase1;
  double phase2;
  
  double hpfilter_cutoff;
  double hpfilter_state[4];

  double lpfilter_cutoff;
  double lpfilter_state[4];

  double amp;
};

static LV2_Handle instantiate(const struct _LV2_Descriptor * descriptor,
			      double                         sample_rate,
			      const char *                   bundle_path,
			      const LV2_Feature *const *     features)
{
  struct synth_t* synth = calloc(1,sizeof(struct synth_t));
  synth->dt = 1.0 / sample_rate;
  for (int i = 0; features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_URI_MAP_URI)) {
      const LV2_URI_Map_Feature * map_feature = features[i]->data;
      synth->midi_event_id =
	map_feature->uri_to_id(map_feature->callback_data,
			       "http://lv2plug.in/ns/ext/event",
			       "http://lv2plug.in/ns/ext/midi#MidiEvent");
    } else if (!strcmp(features[i]->URI, "http://lv2plug.in/ns/ext/event")) {
      synth->event_feature = features[i]->data;
    }
  }
  return synth;
}

static void connect_port(LV2_Handle instance,
			 uint32_t   port,
			 void *     data_location)
{
  struct synth_t* synth = instance;
  switch (port) {
  case PORT_OUTPUT:
    synth->output_p = data_location;
    break;
  case PORT_MIDI:
    synth->midi_in_p = data_location;
    break;
  case PORT_AMP_DECAY:
    synth->amp_decay_p = data_location;
    break;
  case PORT_LPFILTER_DECAY:
    synth->lpfilter_decay_p = data_location;
    break;
  case PORT_HPFILTER_DECAY:
    synth->hpfilter_decay_p = data_location;
    break;
  case PORT_AMP_RELEASE:
    synth->amp_release_p = data_location;
    break;
  case PORT_LPFILTER_RELEASE:
    synth->lpfilter_release_p = data_location;
    break;
  case PORT_HPFILTER_RELEASE:
    synth->hpfilter_release_p = data_location;
    break;
  case PORT_PULSE_WIDTH:
    synth->pulse_width_p = data_location;
    break;
  case PORT_DETUNE:
    synth->detune_p = data_location;
    break;
  default:
    fputs("Warning, unconnected port!\n",stderr);
  }
}

static float midi_to_hz(int note) {
  return 440.0*powf( 2.0, (note-69) / 12.0 );
}

static double fast_exp(double x) {
  return x > -0.5 ? 1 + x : 0.5;
}
static double fast_expm1(double x) {
  return x > -0.5 ? x : -0.5;
}

static double sq(double x) {
  return x * x;
}

static void run(LV2_Handle instance,
		uint32_t   sample_count) {
  struct synth_t* synth = instance;

  lv2_event_begin(&synth->midi_in_iterator, synth->midi_in_p);
  
  for (uint32_t i = 0; i < sample_count; ++i) {
    while(lv2_event_is_valid(&synth->midi_in_iterator)) {
      uint8_t* data;
      LV2_Event* event = lv2_event_get(&synth->midi_in_iterator, &data);
      if (event->type == 0) {
	synth->event_feature->lv2_event_unref(synth->event_feature->callback_data, event);
      } else if (event->type == synth->midi_event_id) {
	if (event->frames > i) {
	  break;
	} else {
	  switch (data[0] & MIDI_COMMANDMASK) {
	  case MIDI_NOTEON:
	    if (data[2] == 0) {
	      synth->num_notes_on -= 1;
	    } else {
	      //	      synth->phase1 = 0;
	      //              synth->phase2 = 0;
	      synth->amp = ((float)data[2])/127.0;
	      synth->freq = midi_to_hz(data[1]);
	      synth->hpfilter_cutoff = 5.0;
	      synth->lpfilter_cutoff = 40000.0;
	      synth->num_notes_on += 1;
	    }
	    break;
	  case MIDI_NOTEOFF:
	    synth->num_notes_on -= 1;
	    break;
	  }
	}
	lv2_event_increment(&synth->midi_in_iterator);
      }
    }
    double amp_decay;
    double lpfilter_decay;
    double hpfilter_decay;
    if (synth->num_notes_on > 0) {
      amp_decay = *synth->amp_decay_p;
      lpfilter_decay = *synth->lpfilter_decay_p;
      hpfilter_decay = *synth->hpfilter_decay_p;
    } else {
      amp_decay = *synth->amp_release_p;
      lpfilter_decay = *synth->lpfilter_release_p;
      hpfilter_decay = *synth->hpfilter_release_p;
    }
    synth->amp *= fast_exp(-synth->dt * amp_decay);
    synth->hpfilter_cutoff *= fast_exp( 0.5 * sq(5000.0 / synth->hpfilter_cutoff) * synth->dt * hpfilter_decay);
    synth->lpfilter_cutoff *= fast_exp(-0.5 * sq(synth->lpfilter_cutoff / 5000.0) * synth->dt * lpfilter_decay);
    synth->phase1 += synth->freq * synth->dt * (1 - *synth->detune_p * 0.01);
    synth->phase2 += synth->freq * synth->dt * (1 + *synth->detune_p * 0.01);
    while (synth->phase1 >= 1.0) { synth->phase1 -= 1.0; }
    while (synth->phase2 >= 1.0) { synth->phase2 -= 1.0; }
    double phase1b = synth->phase1 + *synth->pulse_width_p * 0.01;
    double phase2b = synth->phase2 + *synth->pulse_width_p * 0.01;
    if (phase1b > 1.0) { phase1b -= 1.0;  }
    if (phase2b > 1.0) { phase2b -= 1.0;  }
    double signal = (synth->phase1 - phase1b) + (synth->phase2 - phase2b);
    double hpcoeff = -fast_expm1(-synth->hpfilter_cutoff * M_2_PI * synth->dt);
    for (int i = 0; i < 4; ++i) {
      synth->hpfilter_state[i] += (signal - synth->hpfilter_state[i]) * hpcoeff;
      signal -= synth->hpfilter_state[i];
    }
    double lpcoeff = -fast_expm1(-synth->lpfilter_cutoff * M_2_PI * synth->dt);
    for (int i = 0; i < 4; ++i) {
      signal = synth->lpfilter_state[i] += (signal - synth->lpfilter_state[i]) * lpcoeff;
    }
    synth->output_p[i] = signal * synth->amp;
  }
}

static void cleanup(LV2_Handle instance) {
  free(instance);
}

static const LV2_Descriptor descriptor = {
  .URI = "http://magnus.smartelectronix.com/lv2/synth/qin",
  .instantiate = instantiate,
  .connect_port = connect_port,
  .activate = NULL,
  .run = run,
  .deactivate = NULL,
  .cleanup = cleanup,
  .extension_data = NULL,
};

LV2_SYMBOL_EXPORT const LV2_Descriptor * lv2_descriptor(uint32_t index) {
  switch (index) {
  case 0:
    return &descriptor;
  default:
    return NULL;
  }
}
