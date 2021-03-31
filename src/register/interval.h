#ifndef NATURE_SRC_REGISTER_INTERVAL_H_
#define NATURE_SRC_REGISTER_INTERVAL_H_

#include "src/lir.h"
#include "src/lib/slice.h"

typedef enum {
  LOOP_DETECTION_FLAG_VISITED,
  LOOP_DETECTION_FLAG_ACTIVE,
  LOOP_DETECTION_FLAG_NULL,
} loop_detection_flag;

typedef struct {
  int from;
  int to;
} interval_range;

typedef struct interval {
  lir_operand_var var;
  slice *ranges;
  slice *use_positions;
  struct interval *split_parent;
  slice *split_children;
} interval;

void interval_loop_detection(closure *c);
void interval_block_order(closure *c);
void interval_mark_number(closure *c);
void interval_build_intervals(closure *c);

#endif
