/*
 * pre_update.h
 *
 *  Ported from Telink SDK
 */

#ifndef PRE_UPDATE_H_
#define PRE_UPDATE_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "utilities.h"


/******************************************************************************/
/*                       EXPORT TYPE AND DEFINITION                           */
/******************************************************************************/

typedef struct {
  uint32_t last_t_s;
  uint32_t interval_t_s;
} update_status_periodically_t;

#define MESH_PERIODIC_PUBLISH_RANDOM_TIME 600  // 5 minutes
#define MESH_PERIODIC_PUBLISH_TIME \
  1500  // 25 minutes, periodic_interval: 25min + random(5min)

#define MESH_PERIODIC_PUBLISH_RANDOM_TIME_PW_ON 10
#define MESH_PERIODIC_PUBLISH_TIME_PW_ON 30

/******************************************************************************/
/*                            EXPORTED FUNCTIONS                              */
/******************************************************************************/
void pre_update_reset_time_after_join(void);
void pre_update_init(void);
void pre_update_proc(void);

#endif /* PRE_UPDATE_H_ */
