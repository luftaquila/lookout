# lookout

```
git clone https://github.com/Shreesh-Coder/sam3.git
cd sam3
git checkout feature/macos-cpu-mps
pip install -e .

git clone https://github.com/luftaquila/lookout.git
```

```
crontab -e

# every 30 min, 7:30 AM to 12:00 PM
30 7 * * *      python <PATH_TO_LOOKOUT>/main.py
0,30 8-11 * * * python <PATH_TO_LOOKOUT>/main.py
0 12 * * *      python <PATH_TO_LOOKOUT>/main.py 
```
