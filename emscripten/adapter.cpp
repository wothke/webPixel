/*
* This file adapts "PxTone Collage Collage" & "Organya" to the interface expected by my generic JavaScript player..
*
* Organya seems to be some kind of precursor of PxTone created by Daisuke "Pixel" Amaya.
* I had wrongly assumed that there might be some synergy decoding japaneese song titles to unicode
* but it seems Organya does not have any title info at all.
*
* Copyright (C) 2019 Juergen Wothke
*
* LICENSE the same license used by PxTone is extended to this adapter.
*/

#include <pxtnService.h>
#include <pxtnError.h>

pxtnService *pxtn= 0;
pxtnDescriptor *desc= 0;

int organya_mode= 0;	// pxtone vs organya

#include <stdio.h>
#include <vector>
#include <string>

#include <emscripten.h>

#define BUF_SIZE	1024	// note: organya only uses 576
#define TEXT_MAX	1024
#define NUM_MAX	15


extern "C" char	*sample_buffer=0;
char	*file_buffer=0;
unsigned int	file_buffer_len=0;

const char* info_texts[2];

char title_str[TEXT_MAX];
char desc_str[TEXT_MAX];

// organya
extern "C" void unload_org(void);
extern "C" int org_play(const char *fn, char *buf);
extern "C" int org_getoutputtime(void);
extern "C" int org_currentpos();
extern "C" int org_gensamples();

struct StaticBlock {
    StaticBlock(){
		info_texts[0]= title_str;
		info_texts[1]= desc_str;
    }
};

static StaticBlock staticBlock;

unsigned char isReady= 0;

unsigned long playTime= 0;
unsigned long totalTime= 0;

unsigned int sampleRate= 44100;
unsigned short	channels= 2;

extern "C" void emu_teardown (void)  __attribute__((noinline));
extern "C" void EMSCRIPTEN_KEEPALIVE emu_teardown (void) {
	isReady= 0;
	
	if (organya_mode) {
		if (pxtn) {
			pxtn->clear();
			delete(pxtn);
			pxtn= 0;
			
			delete(desc);
			desc= 0;
		}
	} else {
		unload_org();
	}
	
	
	if (sample_buffer) 	{ free(sample_buffer); sample_buffer= 0; }
	if (file_buffer) 	{ free(file_buffer); file_buffer= 0; file_buffer_len=0; }

}


extern "C" int emu_init(int sample_rate, char *basedir, char *songmodule, void * inBuffer, uint32_t inBufSize) __attribute__((noinline));
extern "C" EMSCRIPTEN_KEEPALIVE int emu_init(int sample_rate, char *basedir, char *songmodule, void * inBuffer, uint32_t inBufSize)
{	
	emu_teardown();
	
	sample_buffer = (char *)calloc(pxtnBITPERSAMPLE / 8 * channels * BUF_SIZE, sizeof(char));	
		
	file_buffer = (char *)calloc(inBufSize, sizeof(char));
	memcpy(file_buffer, inBuffer, inBufSize);
	file_buffer_len= inBufSize;
	
//	sampleRate= sample_rate;	// keep 44100 for the benefit of organya

	char * point;
	organya_mode= ((point = strrchr(songmodule,'.')) != NULL ) && (strcmp(point,".org") == 0);

	bool success = false;
	pxtn= new pxtnService();
	desc= new pxtnDescriptor();
	
	if (organya_mode) {
		success= !org_play((const char *)songmodule, file_buffer);
	} else {
		// pxtone
		if (pxtn->init() == pxtnOK) {		
			if (pxtn->set_destination_quality(2, sample_rate)) {

				if (desc->set_memory_r(file_buffer, inBufSize)) {  
					if(pxtn->read(desc) == pxtnOK) {
						if(pxtn->tones_ready() == pxtnOK) {
							success = true;
						}
					}
				}
				if (!success) {
					pxtn->evels->Release();
				} else {

					pxtnVOMITPREPARATION prep = {0};
					//prep.flags |= pxtnVOMITPREPFLAG_loop; // don't loop
					prep.start_pos_float = 0;
					prep.master_volume = 1; //(volume / 100.0f);

					if (!pxtn->moo_preparation(&prep)) {
						success= false;
					}
				}
			}
		}
	}
	
	return success ? 0 : 1;
}

extern "C" int emu_set_subsong(int subsong) __attribute__((noinline));
extern "C" int EMSCRIPTEN_KEEPALIVE emu_set_subsong(int subsong)
{	
	playTime= 0;
	if (pxtn)
		totalTime= pxtn->moo_get_total_sample();
	
	isReady= 1;
	
	return 0;
}

extern "C" const char** emu_get_track_info() __attribute__((noinline));
extern "C" const char** EMSCRIPTEN_KEEPALIVE emu_get_track_info() {
    if (isReady) {
		memset(title_str, 0, TEXT_MAX);
		memset(desc_str, 0, TEXT_MAX);

		if (!organya_mode) {
			int32_t t_len = 0;		
			const char *title= pxtn->text->get_name_buf(&t_len);
			int32_t c_len = 0;		
			const char *comment= pxtn->text->get_comment_buf(&c_len);

			// iconv based mapping does not seem to wprk in emscripten 
			// therefore handling it on the JavaScript side
			
			if (t_len >= TEXT_MAX) t_len= TEXT_MAX-1;
			if (c_len >= TEXT_MAX) c_len= TEXT_MAX-1;
					
			memcpy(title_str, title, t_len);
			memcpy(desc_str, comment, t_len);
		}		
    }
    return info_texts;
}

extern "C" char* EMSCRIPTEN_KEEPALIVE emu_get_audio_buffer(void) __attribute__((noinline));
extern "C" char* EMSCRIPTEN_KEEPALIVE emu_get_audio_buffer(void) {
	return (char*)sample_buffer;
}

extern "C" long EMSCRIPTEN_KEEPALIVE emu_get_audio_buffer_length(void) __attribute__((noinline));
extern "C" long EMSCRIPTEN_KEEPALIVE emu_get_audio_buffer_length(void) {
	return 	organya_mode ? 576 : BUF_SIZE;		// organya uses hard coded..
}

extern "C" int emu_compute_audio_samples() __attribute__((noinline));
extern "C" int EMSCRIPTEN_KEEPALIVE emu_compute_audio_samples() {
	if (!isReady) return 0;		// don't trigger a new "song end" while still initializing
		
	if (organya_mode) {
		if (!org_gensamples()) return 1;
	} else {
		if (!pxtn->Moo(sample_buffer, pxtnBITPERSAMPLE / 8 * channels * BUF_SIZE)) {
			return 1; // end
		} else {
			playTime+= BUF_SIZE;
		}
	}				
		
	return 0;
}

extern "C" int emu_get_current_position() __attribute__((noinline));
extern "C" int EMSCRIPTEN_KEEPALIVE emu_get_current_position() {
	if (organya_mode) {
		return isReady ? org_currentpos() : -1;			
	} else {
		return isReady ? playTime / sampleRate*1000 : -1;	// time in msecs	
	}
}

extern "C" void emu_seek_position(int pos) __attribute__((noinline));
extern "C" void EMSCRIPTEN_KEEPALIVE emu_seek_position(int pos) {
	// not implemented
}

extern "C" int emu_get_max_position() __attribute__((noinline));
extern "C" int EMSCRIPTEN_KEEPALIVE emu_get_max_position() {
	if (organya_mode) {
		return isReady ? org_getoutputtime() : -1;			
	} else {
		return isReady ? totalTime/sampleRate*1000 : -1;	// time in msecs
	}
}

// not implemented yet
extern "C" int32_t**  emu_get_scope_buffers() {
	return (int32_t**)0;
}
extern "C" int emu_number_trace_streams() __attribute__((noinline));
extern "C" int EMSCRIPTEN_KEEPALIVE emu_number_trace_streams() {
	return 0;	
}
extern "C" const char** emu_trace_streams() __attribute__((noinline));
extern "C" const char** EMSCRIPTEN_KEEPALIVE emu_trace_streams() {
	return (const char**)0;
}

