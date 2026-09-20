#include <stdint.h>
#include <stdio.h>

#include "len.h"

static int failures = 0;

static void expect_instruction_length(const char *name, uint8_t *bytes,
                                      int expected)
{
  int actual = read_instruction64(bytes);
  if(actual != expected){
    fprintf(stderr, "%s: expected %d bytes, got %d\n", name, expected, actual);
    ++failures;
  }
}

static void expect_hook_prefix(const char *name, uint8_t *bytes,
                               int expected)
{
  int decoded = 0;
  while(decoded < 13){
    int length = read_instruction64(bytes + decoded);
    if(length <= 0){
      fprintf(stderr, "%s: decoder failed at offset %d\n", name, decoded);
      ++failures;
      return;
    }
    decoded += length;
  }

  if(decoded != expected){
    fprintf(stderr, "%s: expected hook prefix of %d bytes, got %d\n",
            name, expected, decoded);
    ++failures;
  }
}

int main(void)
{
  uint8_t test_mem_imm8[] = {0xF6, 0x06, 0x01};
  uint8_t test_reg_imm8[] = {0xF6, 0xC0, 0x01};
  uint8_t not_mem[] = {0xF6, 0x16};
  uint8_t stable_prologue[] = {
    0x55, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48,
    0x83, 0xEC, 0x18, 0x41, 0x89, 0xCE
  };
  uint8_t beta_1244_prologue[] = {
    0x55, 0x41, 0x56, 0x53, 0x48, 0x83, 0xEC,
    0x20, 0x89, 0xCB, 0xF6, 0x06, 0x01
  };

  expect_instruction_length("F6 /0 memory TEST", test_mem_imm8, 3);
  expect_instruction_length("F6 /0 register TEST", test_reg_imm8, 3);
  expect_instruction_length("F6 /2 memory NOT", not_mem, 2);
  expect_hook_prefix("stable X-Plane prologue", stable_prologue, 13);
  expect_hook_prefix("X-Plane 12.4.4-b1 prologue", beta_1244_prologue, 13);

  if(failures != 0){
    return 1;
  }

  puts("len64 regression tests passed");
  return 0;
}
