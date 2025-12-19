#!/usr/bin/env python3

# Claude made this.

import sys
import re
from typing import List, Tuple, Optional

def parse_macro_line(line: str) -> Tuple[str, List[str]] | None:
    """Parse a macro invocation line into (macro_name, [args])"""
    line = line.strip()
    if not line or line.startswith('//') or line.startswith('#'):
        return None
    
    match = re.match(r'(\w+)\((.*)\)', line)
    if not match:
        return None
    
    macro_name = match.group(1)
    args_str = match.group(2)
    
    # Split arguments by comma, handling nested parentheses
    args = []
    current_arg = []
    paren_depth = 0
    
    for char in args_str:
        if char == ',' and paren_depth == 0:
            args.append(''.join(current_arg).strip())
            current_arg = []
        else:
            if char == '(':
                paren_depth += 1
            elif char == ')':
                paren_depth -= 1
            current_arg.append(char)
    
    if current_arg:
        args.append(''.join(current_arg).strip())
    
    return macro_name, args

def parse_comment_header(line: str) -> List[str] | None:
    """Parse a comment line as CSV column labels"""
    line = line.strip()
    if not line.startswith('//'):
        return None
    
    # Remove the comment prefix and parse as CSV
    content = line[2:].strip()
    if not content:
        return None
    
    # Simple CSV split
    labels = [label.strip() for label in content.split(',')]
    return labels if labels else None

def format_macro_groups(lines: List[str]) -> str:
    """Format macro groups with aligned columns"""
    # Parse all lines and group by macro name
    groups = []
    current_group = []
    current_macro = None
    pending_comment = None
    
    for i, line in enumerate(lines):
        parsed = parse_macro_line(line)
        
        if parsed:
            macro_name, args = parsed
            if current_macro != macro_name:
                if current_group:
                    groups.append((current_macro, None, current_group))
                current_group = []
                current_macro = macro_name
                
                # Check if the previous line was a comment header
                if i > 0:
                    prev_line = lines[i - 1].strip()
                    labels = parse_comment_header(prev_line)
                    if labels:
                        pending_comment = (prev_line, labels)
            
            current_group.append(args)
        else:
            # Non-macro line (comment, blank, etc.)
            if current_group:
                groups.append((current_macro, pending_comment, current_group))
                current_group = []
                current_macro = None
                pending_comment = None
            
            # Only add non-macro lines that aren't about to be used as headers
            # Check if next line starts a new macro group
            is_header_comment = False
            if i + 1 < len(lines):
                next_parsed = parse_macro_line(lines[i + 1])
                if next_parsed and parse_comment_header(line):
                    is_header_comment = True
            
            if not is_header_comment:
                groups.append((None, None, [line]))
    
    if current_group:
        groups.append((current_macro, pending_comment, current_group))
    
    # Format each group
    result = []
    
    for macro_name, comment_header, items in groups:
        if macro_name is None:
            # Pass through non-macro lines
            result.extend(items)
        else:
            # Calculate column widths for this group
            if not items:
                continue
            
            num_cols = max(len(args) for args in items)
            col_widths = [0] * num_cols
            
            for args in items:
                for i, arg in enumerate(args):
                    col_widths[i] = max(col_widths[i], len(arg))
            
            # If we have a comment header, also consider its width
            if comment_header:
                _, labels = comment_header
                for i, label in enumerate(labels):
                    if i < len(col_widths):
                        col_widths[i] = max(col_widths[i], len(label))
            
            # Format the comment header if present
            if comment_header:
                comment_line, labels = comment_header
                # Account for the macro name and opening paren length
                macro_prefix_len = len(macro_name) + 1  # +1 for '('
                macro_prefix_len -= 2 # for the '//' in comment
                
                formatted_labels = []
                for i, label in enumerate(labels):
                    if i < len(labels) - 1 and i < len(col_widths):
                        formatted_labels.append(label.ljust(col_widths[i]))
                    else:
                        formatted_labels.append(label)
                
                # Add padding before the first label to align with macro arguments
                padded_labels = ' ' * macro_prefix_len + ', '.join(formatted_labels)
                result.append(f"//{padded_labels}")
            
            # Format each macro invocation
            for args in items:
                formatted_args = []
                for i, arg in enumerate(args):
                    if i < len(args) - 1:
                        # Pad all but the last argument
                        formatted_args.append(arg.ljust(col_widths[i]))
                    else:
                        # Don't pad the last argument
                        formatted_args.append(arg)
                
                result.append(f"{macro_name}({', '.join(formatted_args)})")
    
    return '\n'.join(result)

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <xmacro_file>", file=sys.stderr)
        sys.exit(1)
    
    filename = sys.argv[1]
    
    try:
        with open(filename, 'r') as f:
            lines = [line.rstrip() for line in f]
    except FileNotFoundError:
        print(f"Error: File '{filename}' not found", file=sys.stderr)
        sys.exit(1)
    
    formatted = format_macro_groups(lines)
    with open(filename, 'w') as f:
        f.write(formatted + '\n')

if __name__ == '__main__':
    main()