/*
 * Minimal WebRTC SPL stub — provides macros used by ns_core.c.
 * Only WEBRTC_SPL_SAT and the Word16 min/max bounds are needed.
 */
#ifndef WEBRTC_SPL_H_
#define WEBRTC_SPL_H_

#include <stdint.h>

#define WEBRTC_SPL_WORD16_MAX   32767
#define WEBRTC_SPL_WORD16_MIN   (-32768)

/* Saturate b into [c, a]. */
#define WEBRTC_SPL_SAT(a, b, c) ((b) > (a) ? (a) : ((b) < (c) ? (c) : (b)))

#endif /* WEBRTC_SPL_H_ */
