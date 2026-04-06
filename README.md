# lookout

## Wiring
```
       [SW]                       ┌─────────┐ 
 +12V ──o/──┬──────────────────┬──┤(+)      │ 
            │                  ─  │        o── NO
            │          1n4007  ^  │        o── COM
            │                  ─  │        o── NC
            │                  ├──┤(-)      │ 
            │                  │  └─────────┘ 
      ┌───────────┐            │    G6S-2-12  
      │   [VIN]   │            │
      │  HC-SR501 │            │ (C)
      │           │       (B)│/ 
      │      [SIG]o──[10k]───┤   2n3904
      │           │          │\          
      │   [GND]   │            │ (E)
      └───────────┘            │
            └──────────────────┴─────── GND
```

## Setup
```bash
git clone https://github.com/luftaquila/lookout.git
cd lookout
cp .env.example .env

python -m venv venv
source venv/bin/activate
pip install -r requirements.txt

python main.py
```
