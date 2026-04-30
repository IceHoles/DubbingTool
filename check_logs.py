import os

with open('src/core/workflowmanager.cpp', 'r', encoding='utf-8') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'logMessage' in line and '\u041e\u0448\u0438\u0431\u043a\u0430' in line: # Ошибка
        print(f"Found error at line {i}: {line.strip()}")
        break
