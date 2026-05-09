#!/bin/bash
cat dutch_prompt.txt sample.txt | ollama run translategemma:27b
