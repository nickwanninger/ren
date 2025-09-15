#!/usr/bin/env bash


for file in flecs.h flecs.c; do
  wget https://raw.githubusercontent.com/SanderMertens/flecs/refs/heads/master/distr/$file -O $file
done