### Simple api data

## Getting time
curl http://<ip_clock>/api/time

## Formated time
curl -s http://<ip_clock>/api/time | python3 -m json.tool

## Only time in the line
curl -s http://<ip_clock>/api/time | grep -o '"time":"[^"]*"'

## Full stats
curl http://<ip_clock>/api/stats

## Reboot of the clok:
curl http://<ip_clock>//api/reboot

## Brightess:
curl -X POST http://<ip_clock>/api/brightness -d "value=50" or "auto=1"

