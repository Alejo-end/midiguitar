import re

with open('/Users/alejandrodeleon/Documents/NewMedia_Master/digitalFabrication/midiguitar/midiguitar.kicad_sch', 'r') as f:
    content = f.read()

# Extract all global labels with their coordinates
print("=== GLOBAL LABELS ===")
global_labels = re.findall(r'\(global_label "([^"]+)"\s+\(shape \w+\)\s+\(at ([0-9.]+) ([0-9.]+)', content)
for label, x, y in global_labels:
    print(f"  {label}: ({x}, {y})")

# Extract all no_connect markers
print("\n=== NO_CONNECT MARKERS ===")
no_connects = re.findall(r'\(no_connect\s+\(at ([0-9.]+) ([0-9.]+)\)', content)
for x, y in no_connects:
    print(f"  ({x}, {y})")

# Extract all power symbols
print("\n=== POWER SYMBOLS (PWR?? labels) ===")
pwr_symbols = re.findall(r'\(property "Reference" "(#PWR\d+)".*?\(at ([0-9.]+) ([0-9.]+)', content, re.DOTALL)
for ref, x, y in pwr_symbols:
    print(f"  {ref}: ({x}, {y})")

# Find component positions
print("\n=== COMPONENT POSITIONS ===")
components = {
    'M1': re.search(r'\(symbol.*?lib_id "PCM_fab:Module_XIAO-ESP32S3".*?\(at ([0-9.]+) ([0-9.]+)', content, re.DOTALL),
    'U1': re.search(r'\(symbol.*?lib_id "2026-04-11_16-51-34:SN74LVC1T45DBVR".*?\(at ([0-9.]+) ([0-9.]+)', content, re.DOTALL),
    'S1': re.search(r'\(symbol.*?lib_id "PEC12R-4015F-S0024:PEC12R-4015F-S0024".*?\(at ([0-9.]+) ([0-9.]+)', content, re.DOTALL),
    'SW1': re.search(r'\(symbol.*?lib_id "PCM_fab:Switch_Tactile_CUIDevices".*?\(at ([0-9.]+) ([0-9.]+)', content, re.DOTALL),
    'C1': re.search(r'\(property "Reference" "C1".*?\(at ([0-9.]+) ([0-9.]+).*?100nF', content, re.DOTALL),
    'C2': re.search(r'\(property "Reference" "C2".*?\(at ([0-9.]+) ([0-9.]+).*?100nF', content, re.DOTALL),
    'R4': re.search(r'\(property "Reference" "R4".*?\(at ([0-9.]+) ([0-9.]+)', content, re.DOTALL),
    'R5': re.search(r'\(property "Reference" "R5".*?\(at ([0-9.]+) ([0-9.]+)', content, re.DOTALL),
}

for comp, match in components.items():
    if match:
        print(f"  {comp}: ({match.group(1)}, {match.group(2)})")

