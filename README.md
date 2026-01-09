# lookout

```
git clone https://github.com/Shreesh-Coder/sam3.git
cd sam3
git checkout feature/macos-cpu-mps
pip install -e .

git clone https://github.com/luftaquila/lookout.git
cp .env.example .env
```

```
crontab -e

# every 30 min, 7:30 AM to 12:00 PM on weekdays
30 7 * * 1-5      python <PATH_TO_LOOKOUT>/main.py
0,30 8-11 * * 1-5 python <PATH_TO_LOOKOUT>/main.py
0 12 * * 1-5      python <PATH_TO_LOOKOUT>/main.py 
```
