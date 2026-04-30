import os
import re

def process_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    pattern = re.compile(r'(emit\s+)?(logMessage\s*\()([^;]+)(\);)', re.DOTALL)
    
    def replacer(match):
        emit_part = match.group(1) or ''
        func_call = match.group(2)
        args_str = match.group(3)
        end_part = match.group(4)
        
        lower_args = args_str.lower()
        if 'loglevel::' in args_str or 'const qstring' in args_str:
            return match.group(0)
            
        level = None
        if 'ошибка' in lower_args:
            level = 'LogLevel::Error'
        elif 'предупрежд' in lower_args:
            level = 'LogLevel::Warning'
            
        if level:
            if 'LogCategory::' in args_str:
                new_args = f'{args_str}, {level}'
            else:
                new_args = f'{args_str}, LogCategory::APP, {level}'
            return f'{emit_part}{func_call}{new_args}{end_part}'
        return match.group(0)
        
    new_content = pattern.sub(replacer, content)
    if new_content != content:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(new_content)
        print(f'Updated {filepath}')

for root, _, files in os.walk('src'):
    for f in files:
        if f.endswith('.cpp') or f.endswith('.h'):
            process_file(os.path.join(root, f))
