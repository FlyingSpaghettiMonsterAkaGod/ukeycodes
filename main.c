/***************************************************************************
 *   Copyright (C) by Flying Spaghetti Monster also known as God           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License version 3        *
 *   as published by the Free Software Foundation;                         *
 *                                                                         *
 *  This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY so                                           *
 *                                                                         *
 * If something will goes wrong you can always make suicide                *
 * eg:                                                                     *
 *   1. Shoot your head with shotgun                                       *
 *   2. Drink poison                                                       *
 *   3. Take a lethal dose of drugs                                        *
 *   4. Eat your own head                                                  *
 *                                                                         *
 * Если что-то пойдет не так вы всегда можете убить себя                   *
 * например:                                                               *
 *   1. Выстрелить себе в голову из дробовика                              *
 *   2. Выпить яд                                                          *
 *   3  Принять смертельную дозу наркотиков                                *
 *   3. Съесть свою голову                                                 *
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <regex.h>

char* g_version_string = "0.1";

int open_input(const char* input_device_path)
{
  int fd = open(input_device_path, O_RDONLY);
  if(fd == -1)
  {
    fprintf(stderr, "error: failed to open input device(%s)\n", input_device_path);
    return -1;
  }

  const int buf_size = 1024;
  char buf[buf_size];
  ioctl(fd, EVIOCGNAME(buf_size), buf);
  printf("input device opened: %s (%s)\n", input_device_path, buf);

  if(ioctl(fd, EVIOCGRAB, 1) != 0)
  {
    fprintf(stderr, "error: cannot grab input device(%s)\n", input_device_path);
    fd = -1;
  }

  return fd;
}

int create_output(const char* dev_uinput_path, const char* dev_name)
{
  int fd = open(dev_uinput_path, O_WRONLY | O_NONBLOCK);
  if(fd == -1)
  {
    fprintf(stderr, "error: cannot open uinput device(%s)\n", dev_uinput_path);
    return fd;
  }

  int ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) != -1;

  for(int code = 0; code < KEY_MAX; code++)
  {
    ok = ok && ioctl(fd, UI_SET_KEYBIT, code) != -1;
  }

  if(ok)
  {
    struct uinput_user_dev uidev;
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "%s", dev_name);
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 1;
    uidev.id.product = 1;
    uidev.id.version = 1;
    ok = ok && write(fd, &uidev, sizeof(uidev)) != -1;
  }

  ok = ok && ioctl(fd, UI_DEV_CREATE) != -1;

  if(ok)
  {
    printf("uinput device created\n");
  }
  else
  {
    fprintf(stderr, "error: failed to initialize uinput device\n");
  }

  return ok ? fd : -1;
}

struct Args
{
  const char* dev_input_event_path;
  const char* dev_uinput_name;
  const char* dev_uinput_path;
  const char* remap_rules_string;
  int* keycodes_map;
  int help;
  int verbose;
};

int get_option(const char* option_name, int argc, char* argv[], int* index, const char** option_return)
{
  int is_matched = 0;

  int option_name_length = strlen(option_name);

  if(strncmp(argv[*index], option_name, option_name_length) == 0)
  {
    is_matched = 1;
    if(option_return)
    {
      int arg_len = strlen(argv[*index]);

      if(arg_len > option_name_length) { *option_return = argv[*index] + option_name_length; }
      else if(++(*index) < argc)       { *option_return = argv[*index]; }
    }
  }

  return is_matched;
}

void print_help()
{
  printf("ukeycodes %s - remapping keycodes of /dev/input/event devices\n"
         "usage: ukeycodes /dev/input/eventX [options]\n\n"
         "options:\n"
         "  -h, --help  this message\n"
         "  -r  original_code=remapped_code[:sequence_length]\n"
         "              eg: ukeycodes /dev/input/event0 -r \"2=3 3=4 5=6 6=7\"\n"
         "                  ukeycodes /dev/input/event0 -r \"2=3:4\"\n"
         "  -u          path to uinput device (default: /dev/uinput)\n"
         "  -n          name for uinput device (default: Custom input device)\n"
         "  -v          verbose mode: print input codes\n\n", g_version_string
         );
}

int rx_match_to_int(const char* source, regmatch_t match)
{
  char buf[1024];

  int length = match.rm_eo - match.rm_so;
  strncpy(buf, source + match.rm_so, length);
  buf[length] = 0;

  return atoi(buf);
}

struct Args process_args(int argc, char* argv[])
{
  if(argc < 2)
  {
    printf("input device not specified\n");
    exit(1);
  }

  struct Args args;
  memset(&args, 0, sizeof(args));

  args.keycodes_map = (int*)calloc(KEY_CNT, sizeof(int));
  args.dev_input_event_path = argv[1];

  for(int i = 1; i < argc; i++)
  {
    if(get_option("-h"    , argc, argv, &i, 0) ||
       get_option("--help", argc, argv, &i, 0))
    {
      args.help = 1;
      break;
    }

    if(get_option("-n", argc, argv, &i, &args.dev_uinput_name)) continue;
    if(get_option("-u", argc, argv, &i, &args.dev_uinput_path)) continue;
    if(get_option("-v", argc, argv, &i, 0))
    {
      args.verbose = 1;
      continue;
    }

    if(get_option("-r", argc, argv, &i, &args.remap_rules_string))
    {
      regex_t rx_split_mappings;
      size_t     matches_size = 5;
      regmatch_t matches[matches_size];

      if(regcomp(&rx_split_mappings, "\\([0-9]\\+\\)\\s*[=]\\s*\\([0-9]\\+\\)\\(\\|\\s*:\\s*\\([0-9]\\+\\)\\)", 0) != 0)
      {
        fprintf(stderr, "error: cannot parse options (regcomp failed to compile)\n");
        exit(1);
      }

      int offset = 0;

      while(regexec(&rx_split_mappings, args.remap_rules_string + offset, matches_size, matches, 0) == 0)
      {
        int code1 = rx_match_to_int(args.remap_rules_string + offset, matches[1]);
        int code2 = rx_match_to_int(args.remap_rules_string + offset, matches[2]);

        int num_autofill = rx_match_to_int(args.remap_rules_string + offset, matches[4]);

        num_autofill = num_autofill ? num_autofill : 1;
        for(int i = 0; i < num_autofill; i++)
        {
          args.keycodes_map[code1++] = code2++;
        }

        offset += matches[2].rm_eo;
      }

      continue;
    }
  }

  if(args.dev_uinput_name == 0) { args.dev_uinput_name = "Custom input device"; }
  if(args.dev_uinput_path == 0) { args.dev_uinput_path = "/dev/uinput"; }

  return args;
}

int main(int argc, char* argv[])
{
  struct Args args = process_args(argc, argv);

  if(args.help)
  {
    print_help();
    exit(0);
  }

  int input_fd  = open_input(args.dev_input_event_path);
  int output_fd = input_fd != -1 ? create_output(args.dev_uinput_path, args.dev_uinput_name) : -1;
  if(output_fd == -1)
  {
    exit(1);
  }

  printf("remap rules:\n");
  for(int code = 0; code < KEY_CNT; code++)
  {
    int remap_code = args.keycodes_map[code];
    if(remap_code)
    {
       printf("%d = %d\n", code, remap_code);
    }
  }

  struct input_event event;
  size_t event_size = sizeof(struct input_event);

  for(;;)
  {
    int read_size = read(input_fd, &event, event_size);
    if(read_size)
    {
      if(args.verbose)
      {
        printf("code:%d value:%d type:%d\n", event.code, event.value, event.type);
      }

      if(event.type == EV_KEY)
      {
        int remapped_code = args.keycodes_map[event.code];
        if(remapped_code != 0)
        {
          event.code = remapped_code;
        }
      }

      if(write(output_fd, &event, event_size) == -1)
      {
        fprintf(stderr, "error: write into uinput device failed\n");
      }
    }
  }

  ioctl(input_fd, EVIOCGRAB, 0);
  close(input_fd);
  close(output_fd);
  free(args.keycodes_map);

  return 0;
}
