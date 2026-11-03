del *.stb
del *.xml
del greed\*.stb
del greed\*.xml

py -3 "../../../../tools/BR scripts/roommergerconfig.py" mergerConfig1.json
py -3 "../../../../tools/BR scripts/roommergerconfig.py" mergerConfig2.json

del *.xml
del greed\*.xml
del "fragment rooms\*.xml"
del /s "fragment rooms\*.lua"
del /s "fragment rooms\*.stb"

py -3 "../../../../tools/BR scripts/roomstatisticsconfig.py" statisticsConfig.json

pause
