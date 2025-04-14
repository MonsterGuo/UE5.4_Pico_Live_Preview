# 一个基于Pico Live Preview 更改的版本修复了部分功能
## 版本概述
有以下修善：
1. 新增一档画质选项“EPIC”.
2. 修正原来“中/低”了错误颜色映射问题
3. 修正了摇杆，“按下”与“触摸” 对掉问题
4. 修改PC端窗口不能自定义的问题
5. 修正了“A/B/X/Y/肩键/侧键”的触摸丢失问题
6. 为了能正确映射肩键。侧键新增两对键值

待改善的：（希望官方尽早修复）
1. 按键冲突的问题，这个是似乎没有适配（增强输入）
2. 进程无法自动关闭的问题（目前只能通过任务栏关闭UE进程）

## 多质量等级 
新增EPIC质量级别  
### 各个配置对应的渲染分辨率
EPIC： 4320x2160  
High: 3840 x 1920  
Medium: 2880 x 1440  
Low: 2400 x 1200 

![Snipaste_2025-04-14_13-52-54](https://github.com/user-attachments/assets/f6e97552-0e55-4340-a372-640074a2efb9) 

## 修正“中/低”质量党Gamma异常 
异常
![Screenshot_com picovr picostreamassistant_2025 04 02-22 27 19 527_104](https://github.com/user-attachments/assets/ff328dbb-366b-450b-aaef-f8498c8fe535) 
正常
![Screenshot_com picovr picostreamassistant_2025 04 02-22 28 21 136_454](https://github.com/user-attachments/assets/4d9b974c-1e76-47f6-8b39-fe7916915123)

## 按键修正 
按键修正部分内容

PICO Touch (L/R) Thumbrest Touch  
PICO Touch (L/R) Y/B Touch  
PICO Touch (L/R) X/A Touch  
PICO Touch (L/R) Thumbstick Touch  
PICO Touch (L/R) Trigger Touch  

新增两个按键用于测试    
 （正式使用请使用原有按键）   
PICO Touch (L/R)  Grip Axis Anime  
PICO Touch (L/R) Trigger Axis Anime   

![Snipaste_2025-04-14_15-23-02](https://github.com/user-attachments/assets/ff813058-06ae-4fbb-9b54-23ae7e0db943)
