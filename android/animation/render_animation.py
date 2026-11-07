import os
import math
import shutil
import subprocess
import numpy as np
from PIL import Image, ImageDraw

FPS = 30
DURATION = 1.5 # 1.5s per loop cycle
TOTAL_FRAMES = int(FPS * DURATION) # 45 frames
OUT_SIZE = 512

WORKSPACE_DIR = r'c:\Users\karth\Downloads\yt downloader'
FRAMES_DIR = os.path.join(WORKSPACE_DIR, 'anim_frames')
os.makedirs(FRAMES_DIR, exist_ok=True)

# Original logo image
LOGO_PATH = r'C:/Users/karth/.gemini/antigravity/brain/31dceb70-3525-4def-b1d7-b8e085723c9e/.user_uploaded/media_1794050772451_8085f992.png'
if not os.path.exists(LOGO_PATH):
    # Fallback to local copy if available
    LOGO_PATH = os.path.join(WORKSPACE_DIR, 'clean_button.png')

orig = Image.open(LOGO_PATH).convert('RGBA')
arr = np.array(orig)

# 1. Segment layers with high precision
white_mask = (arr[:,:,0] > 200) & (arr[:,:,1] > 200) & (arr[:,:,2] > 200) & (arr[:,:,3] > 100)

arrow_mask = np.zeros_like(white_mask)
tray_mask = np.zeros_like(white_mask)

for y in range(len(arr)):
    for x in range(len(arr[0])):
        if white_mask[y, x]:
            if y < 550:
                arrow_mask[y, x] = True
            elif y <= 585 and (460 <= x <= 565):
                arrow_mask[y, x] = True
            else:
                tray_mask[y, x] = True

arrow_arr = np.zeros_like(arr)
arrow_arr[arrow_mask] = arr[arrow_mask]
arrow_img = Image.fromarray(arrow_arr)

tray_arr = np.zeros_like(arr)
tray_arr[tray_mask] = arr[tray_mask]
tray_img = Image.fromarray(tray_arr)

mask_btn = np.zeros((1024, 1024), dtype=bool)
for y in range(330, 690):
    for x in range(270, 755):
        if arr[y,x,3] > 100:
            is_red = (arr[y,x,0] > 150 and arr[y,x,1] < 90)
            is_dark = (arr[y,x,0] < 70 and arr[y,x,1] < 70 and arr[y,x,2] < 70)
            is_white = white_mask[y,x]
            if is_red or is_dark or is_white:
                mask_btn[y,x] = True

# Cloud layer (isolated)
cloud_arr = arr.copy()
cloud_arr[mask_btn] = [0, 0, 0, 0]
cloud_img = Image.fromarray(cloud_arr)

# Clean red button layer
btn_clean_arr = arr.copy()
for y in range(350, 670):
    for x in range(300, 725):
        if mask_btn[y, x] and not (arr[y,x,0] < 60 and arr[y,x,1] < 60 and arr[y,x,2] < 60):
            btn_clean_arr[y, x] = [229, 46, 46, 255]
btn_clean_arr[~mask_btn] = [0, 0, 0, 0]
btn_clean_img = Image.fromarray(btn_clean_arr)

# Arrow sprite transformations (squash & stretch)
arrow_bbox = arrow_img.getbbox()
arrow_crop = arrow_img.crop(arrow_bbox)
anchor_x, anchor_y = 512, 575

def get_arrow_sprite(sx, sy):
    if abs(sx - 1.0) < 0.005 and abs(sy - 1.0) < 0.005:
        return arrow_img
    nw = max(1, int(arrow_crop.width * sx))
    nh = max(1, int(arrow_crop.height * sy))
    scaled = arrow_crop.resize((nw, nh), Image.Resampling.BILINEAR)
    rel_x = anchor_x - arrow_bbox[0]
    rel_y = anchor_y - arrow_bbox[1]
    px = anchor_x - int(rel_x * sx)
    py = anchor_y - int(rel_y * sy)
    spr = Image.new('RGBA', (1024, 1024), (0, 0, 0, 0))
    spr.paste(scaled, (px, py))
    return spr

frames = []
for i in range(TOTAL_FRAMES):
    phi = i / float(TOTAL_FRAMES) # 0.0 to 1.0 (seamless cycle)
    
    # 1. White card background (matching button scale in Image 2)
    frame = Image.new('RGBA', (1024, 1024), (255, 255, 255, 255))
    
    # 2. Cloud Floating & Breathing
    cloud_dy = int(-6.0 * math.sin(2.0 * math.pi * phi))
    cloud_scale = 1.0 + 0.02 * (0.5 - 0.5 * math.cos(2.0 * math.pi * phi))
    
    if abs(cloud_scale - 1.0) > 0.001:
        nw, nh = int(1024 * cloud_scale), int(1024 * cloud_scale)
        scaled_c = cloud_img.resize((nw, nh), Image.Resampling.BILINEAR)
        frame.paste(scaled_c, ((1024 - nw)//2, (1024 - nh)//2 + cloud_dy), scaled_c)
    else:
        frame.paste(cloud_img, (0, cloud_dy), cloud_img)

    # 3. Red TV Button
    btn_dy = int(cloud_dy * 0.6)
    frame.paste(btn_clean_img, (0, btn_dy), btn_clean_img)
    
    # 4. Dynamic Downloader Motion
    if phi < 0.18:
        # Anticipation
        t_ant = phi / 0.18
        arrow_dy = -8.0 * math.sin(math.pi * t_ant)
        sx = 0.98 + 0.02 * (1.0 - math.sin(math.pi * t_ant))
        sy = 1.03 - 0.03 * (1.0 - math.sin(math.pi * t_ant))
        tray_dy = 0.0
    elif phi < 0.44:
        # Power Plunge into tray
        t_surge = (phi - 0.18) / (0.44 - 0.18)
        surge_factor = t_surge * t_surge * (3.0 - 2.0 * t_surge)
        arrow_dy = -8.0 * (1.0 - surge_factor) + 26.0 * surge_factor
        stretch = math.sin(math.pi * t_surge)
        sy = 1.0 + 0.07 * stretch
        sx = 1.0 - 0.04 * stretch
        tray_dy = 0.0
    elif phi < 0.56:
        # Tray Impact & Elastic Cushion
        t_imp = (phi - 0.44) / (0.56 - 0.44)
        imp_pulse = math.sin(math.pi * t_imp)
        arrow_dy = 26.0 + 2.0 * imp_pulse
        tray_dy = 5.0 * imp_pulse
        sy = 1.0 - 0.06 * imp_pulse
        sx = 1.0 + 0.05 * imp_pulse
    else:
        # Damped Rebound & Settle back to origin
        t_ret = (phi - 0.56) / (1.00 - 0.56)
        decay = (1.0 - t_ret) ** 1.8
        arrow_dy = 26.0 * decay * math.cos(math.pi * 0.5 * t_ret)
        tray_dy = 0.0
        sy = 1.0
        sx = 1.0
        
    # Paste tray
    frame.paste(tray_img, (0, btn_dy + int(tray_dy)), tray_img)
    
    # Paste arrow
    spr_arrow = get_arrow_sprite(sx, sy)
    frame.paste(spr_arrow, (0, btn_dy + int(arrow_dy)), spr_arrow)
    
    # 5. Streaming Data Particles Layer
    part_layer = Image.new('RGBA', (1024, 1024), (0, 0, 0, 0))
    draw_part = ImageDraw.Draw(part_layer)
    
    # 2 streams of data packets flowing down beside arrow into tray
    for s_idx, sx_pos in enumerate([462, 562]):
        phase_offset = s_idx * 0.5
        p_phi = (phi + phase_offset) % 1.0
        py_pos = 375 + p_phi * 220
        if p_phi < 0.15:
            p_alpha = p_phi / 0.15
        elif p_phi > 0.85:
            p_alpha = (1.0 - p_phi) / 0.15
        else:
            p_alpha = 1.0
        
        pr = 7
        mint_color = (78, 224, 165, int(230 * p_alpha))
        draw_part.ellipse([sx_pos - pr, py_pos - pr, sx_pos + pr, py_pos + pr], fill=mint_color)
        white_core = (255, 255, 255, int(240 * p_alpha))
        draw_part.ellipse([sx_pos - pr//2, py_pos - pr//2, sx_pos + pr//2, py_pos + pr//2], fill=white_core)

    # Impact ripples at tray bottom
    if 0.44 <= phi <= 0.65:
        imp_t = (phi - 0.44) / 0.21
        imp_alpha = math.sin(math.pi * imp_t) * 0.6
        r_w = int(10 + 20 * imp_t)
        r_h = int(5 + 10 * imp_t)
        rgba = (78, 224, 165, int(255 * imp_alpha))
        draw_part.ellipse([395 - r_w, 615 - r_h, 395 + r_w, 615 + r_h], outline=rgba, width=3)
        draw_part.ellipse([625 - r_w, 615 - r_h, 625 + r_w, 615 + r_h], outline=rgba, width=3)

    frame.paste(part_layer, (0, btn_dy), part_layer)
    
    frame_512 = frame.resize((OUT_SIZE, OUT_SIZE), Image.Resampling.LANCZOS)
    frame_path = os.path.join(FRAMES_DIR, f'frame_{i:03d}.png')
    frame_512.save(frame_path)
    frames.append(frame_512)

# Save WebP
webp_path = os.path.join(WORKSPACE_DIR, 'download_animated.webp')
frames[0].save(webp_path, save_all=True, append_images=frames[1:], duration=int(1000/FPS), loop=0)

# Save GIF
gif_path = os.path.join(WORKSPACE_DIR, 'download_animated.gif')
frames[0].save(gif_path, save_all=True, append_images=frames[1:], duration=int(1000/FPS), loop=0)

# Copy WebP to Android res/drawable
android_drawable = os.path.join(WORKSPACE_DIR, 'android', 'app', 'src', 'main', 'res', 'drawable', 'download_animated.webp')
shutil.copyfile(webp_path, android_drawable)

# Generate MP4 videos via ffmpeg
mp4_path = os.path.join(WORKSPACE_DIR, 'download_animated.mp4')
single_mp4_path = os.path.join(WORKSPACE_DIR, 'download_loop_single.mp4')

subprocess.run([
    'ffmpeg', '-y',
    '-framerate', str(FPS),
    '-i', os.path.join(FRAMES_DIR, 'frame_%03d.png'),
    '-c:v', 'libx264',
    '-pix_fmt', 'yuv420p',
    '-crf', '16',
    single_mp4_path
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

subprocess.run([
    'ffmpeg', '-y',
    '-stream_loop', '3', # 4 seamless cycles = 6 seconds
    '-framerate', str(FPS),
    '-i', os.path.join(FRAMES_DIR, 'frame_%03d.png'),
    '-c:v', 'libx264',
    '-pix_fmt', 'yuv420p',
    '-crf', '16',
    mp4_path
], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

print("Generated all files successfully:")
print(" - Animated WebP:", webp_path)
print(" - Looping GIF:", gif_path)
print(" - Video (6s loop):", mp4_path)
print(" - Video (1.5s single):", single_mp4_path)
print(" - Android drawable:", android_drawable)
