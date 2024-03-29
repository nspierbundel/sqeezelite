/* 
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012, 2013, triode1@btinternet.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "squeezelite.h"

extern log_level loglevel;

extern struct buffer *streambuf;
extern struct buffer *outputbuf;
extern struct streamstate stream;
extern struct outputstate output;
extern struct decodestate decode;

#define LOCK_S   mutex_lock(streambuf->mutex)
#define UNLOCK_S mutex_unlock(streambuf->mutex)
#define LOCK_O   mutex_lock(outputbuf->mutex)
#define UNLOCK_O mutex_unlock(outputbuf->mutex)

#define MAX_DECODE_FRAMES 4096

static u32_t sample_rates[] = {
	11025, 22050, 32000, 44100, 48000, 8000, 12000, 16000, 24000, 96000, 88200, 176400, 192000, 352800, 384000
};

static u32_t sample_rate;
static u32_t sample_size;
static u32_t channels;
static bool  bigendian;

static decode_state pcm_decode(void) {
	size_t in, out;
	frames_t frames, count;
	u32_t *optr;
	u8_t  *iptr;
	
	LOCK_S;
	LOCK_O;

	in = min(_buf_used(streambuf), _buf_cont_read(streambuf)) / (channels * sample_size);
	out = min(_buf_space(outputbuf), _buf_cont_write(outputbuf)) / BYTES_PER_FRAME;

	if (stream.state <= DISCONNECT && in == 0) {
		UNLOCK_O;
		UNLOCK_S;
		return DECODE_COMPLETE;
	}

	if (decode.new_stream) {
		LOG_INFO("setting track_start");
		output.next_sample_rate = sample_rate; 
		output.track_start = outputbuf->writep;
		if (output.fade_mode) _checkfade(true);
		decode.new_stream = false;
	}

	frames = min(in, out);
	frames = min(frames, MAX_DECODE_FRAMES);

	optr = (u32_t *)outputbuf->writep;
	iptr = (u8_t *)streambuf->readp;

	count = frames * channels;

	if (channels == 2) {
 		if (sample_size == 2) {
			if (bigendian) {
				while (count--) {
					*optr++ = *(iptr) << 24 | *(iptr+1) << 16;
					iptr += 2;
				}
			} else {
				while (count--) {
					*optr++ = *(iptr) << 16 | *(iptr+1) << 24;
					iptr += 2;
				}
			}
		} else if (sample_size == 3) {
			if (bigendian) {
				while (count--) {
					*optr++ = *(iptr) << 24 | *(iptr+1) << 16 | *(iptr+2) << 8;
					iptr += 3;
				}
			} else {
				while (count--) {
					*optr++ = *(iptr) << 8 | *(iptr+1) << 16 | *(iptr+2) << 24;
					iptr += 3;
				}
			}
		} else if (sample_size == 1) {
			while (count--) {
				*optr++ = *iptr++ << 24;
			}
		}
	} else if (channels == 1) {
 		if (sample_size == 2) {
			if (bigendian) {
				while (count--) {
					*optr = *(iptr) << 24 | *(iptr+1) << 16;
					*(optr+1) = *optr;
					iptr += 2;
					optr += 2;
				}
			} else {
				while (count--) {
					*optr = *(iptr) << 16 | *(iptr+1) << 24;
					*(optr+1) = *optr;
					iptr += 2;
					optr += 2;
				}
			}
		} else if (sample_size == 3) {
			if (bigendian) {
				while (count--) {
					*optr = *(iptr) << 24 | *(iptr+1) << 16 | *(iptr+2) << 8;
					*(optr+1) = *optr;
					iptr += 3;
					optr += 2;
				}
			} else {
				while (count--) {
					*optr = *(iptr) << 8 | *(iptr+1) << 16 | *(iptr+2) << 24;
					*(optr+1) = *optr;
					iptr += 3;
					optr += 2;
				}
			}
		} else if (sample_size == 1) {
			while (count--) {
				*optr = *iptr++ << 24;
				*(optr+1) = *optr;
				optr += 2;
			}
		}
	} else {
		LOG_ERROR("unsupported channels");
	}

	LOG_SDEBUG("decoded %u frames", frames);

	_buf_inc_readp(streambuf, frames * channels * sample_size);
	_buf_inc_writep(outputbuf, frames * BYTES_PER_FRAME);

	UNLOCK_O;
	UNLOCK_S;

	return DECODE_RUNNING;
}

static void pcm_open(u8_t size, u8_t rate, u8_t chan, u8_t endianness) {
	sample_size = size - '0' + 1;
	sample_rate = sample_rates[rate - '0'];
	channels    = chan - '0';
	bigendian   = (endianness == '0');

	LOG_INFO("pcm size: %u rate: %u chan: %u bigendian: %u", sample_size, sample_rate, channels, bigendian);
	buf_adjust(streambuf, sample_size * channels);
}

static void pcm_close(void) {
	buf_adjust(streambuf, 1);
}

struct codec *register_pcm(void) {
	static struct codec ret = { 
		'p',         // id
		"aif,pcm",   // types
		4096,        // min read
		102400,      // min space
		pcm_open,    // open
		pcm_close,   // close
		pcm_decode,  // decode
	};

	LOG_INFO("using pcm");
	return &ret;
}
