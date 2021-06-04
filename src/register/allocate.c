#include "allocate.h"

void allocate(closure *c) {
  list *unhandled = init_unhandled(c);
  list *handled = list_new();
  list *active = list_new();
  list *inactive = list_new();

  while (unhandled->count != 0) {
    interval *current_interval = (interval *) list_pop(unhandled);
    int position = current_interval->first_from; // 不等于 first use position

    // handle active
    list_node *active_curr = active->front;
    list_node *active_prev = NULL;
    while (active_curr->value != NULL) {
      interval *select = (interval *) active_curr->value;
      bool is_expired = select->last_to < position;
      bool is_covers = interval_is_covers(select, position);

      if (is_covers || is_expired) {
        if (active_prev == NULL) {
          active->front = active_curr->next;
        } else {
          active_prev->next = active_curr->next;
        }
        active->count--;
        if (is_expired) {
          list_push(handled, select);
        } else {
          list_push(inactive, select);
        }
      }

      active_prev = active_curr;
      active_curr = active_curr->next;
    }
  }
}

list *init_unhandled(closure *c) {
  list *unhandled = list_new();
  // 遍历所有变量
  for (int i = 0; i < c->globals.count; ++i) {
    void *raw = table_get(c->interval_table, c->globals.list[i]->ident);
    if (raw == NULL) {
      continue;
    }
    interval *item = (interval *) raw;
    to_unhandled(unhandled, item);
  }
  // 遍历所有固定寄存器
  for (int i = 0; i < c->fixed_regs.count; ++i) {
    void *raw = table_get(c->interval_table, c->fixed_regs.list[i]->ident);
    if (raw == NULL) {
      continue;
    }
    interval *item = (interval *) raw;
    to_unhandled(unhandled, item);
  }

  return unhandled;
}

void to_unhandled(list *unhandled, interval *to) {
  // 从头部取出,最小的元素
  list_node *temp = unhandled->front;
  list_node *prev = NULL;
  while (temp->value != NULL && ((interval *) temp->value)->first_from < to->first_from) {
    prev = temp;
    temp = temp->next;
  }

  // temp is rear
  if (temp->value == NULL) {
    list_node *empty = list_new_node();
    temp->value = to;
    temp->next = empty;
    unhandled->rear = empty;
    unhandled->count++;
    return;
  }

  // temp->value->first_from > await_to->first_from > prev->value->first_from
  list_node *await_node = list_new_node();
  await_node->value = to;
  await_node->next = temp;
  unhandled->count++;
  if (prev != NULL) {
    prev->next = await_node;
  }
}

