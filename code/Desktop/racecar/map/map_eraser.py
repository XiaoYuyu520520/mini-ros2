import tkinter as tk
from PIL import Image, ImageTk
import os

# ========== 配置 ==========
PGM_FILE = "ai_map001.pgm"
BRUSH_SIZE = 12       # 画笔大小
COLOR_FREE = 255      # 白色：可通行
COLOR_UNKNOWN = 128    # 灰色：未知区域
# ==========================

# 读取地图
if not os.path.exists(PGM_FILE):
    print(f"❌ 找不到 {PGM_FILE}")
    exit()

img = Image.open(PGM_FILE).convert("L")
img_backup = img.copy()
pixels = img.load()

# 当前画笔模式：默认擦成白色
current_color = COLOR_FREE

root = tk.Tk()
root.title("SLAM地图橡皮擦 | 1=擦白(通行) 2=涂灰(未知) s保存 r重置 q退出")
tk_img = ImageTk.PhotoImage(img)

canvas = tk.Canvas(root, width=img.width, height=img.height)
canvas.pack()
canvas.create_image(0, 0, image=tk_img, anchor=tk.NW)

def paint(event):
    x, y = event.x, event.y
    r = BRUSH_SIZE
    for dx in range(-r, r+1):
        for dy in range(-r, r+1):
            if dx*dx + dy*dy <= r*r:
                nx = x + dx
                ny = y + dy
                if 0 <= nx < img.width and 0 <= ny < img.height:
                    pixels[nx, ny] = current_color
    # 刷新画面
    new_tk = ImageTk.PhotoImage(img)
    canvas.itemconfig(canvas.find_all()[0], image=new_tk)
    canvas.image = new_tk

def start_paint(event):
    paint(event)

# 按键功能
def set_white(event):
    global current_color
    current_color = COLOR_FREE
    print("🖌️ 已切换：擦成 白色(可通行)")

def set_gray(event):
    global current_color
    current_color = COLOR_UNKNOWN
    print("🖌️ 已切换：涂成 灰色(未知区域)")

def save_map():
    img.save(PGM_FILE)
    print(f"✅ 已保存：{PGM_FILE}")

def reset_map():
    global img, pixels
    img = img_backup.copy()
    pixels = img.load()
    new_tk = ImageTk.PhotoImage(img)
    canvas.itemconfig(canvas.find_all()[0], image=new_tk)
    canvas.image = new_tk
    print("🔄 已重置原图")

# 鼠标绑定
canvas.bind("<Button-1>", start_paint)
canvas.bind("<B1-Motion>", paint)

# 键盘快捷键
root.bind("1", set_white)
root.bind("2", set_gray)
root.bind("s", lambda e: save_map())
root.bind("r", lambda e: reset_map())
root.bind("q", lambda e: root.quit())

print("===== SLAM 地图画笔工具 =====")
print("按住鼠标左键涂抹绘画")
print("按 1  → 画笔变白：擦除障碍为可通行区域")
print("按 2  → 画笔变灰：涂抹成 SLAM 未知区域")
print("s 保存   r 重置   q 退出")

root.mainloop()
