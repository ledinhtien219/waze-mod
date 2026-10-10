import re, sys
from PIL import Image
def load(path):
    s = open(path).read()
    bm = s[s.index('glyph_bitmap[] = {')+18 : s.index('};', s.index('glyph_bitmap[] = {'))]
    bitmap = bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})', re.sub(r'/\*.*?\*/','',bm)))
    gd = re.findall(r'\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}', s)
    gd = [tuple(map(int,g)) for g in gd]
    ul = {int(i): [int(x,16) for x in v.split(',') if x.strip()] for i,v in re.findall(r'unicode_list_(\d+)\[\] = \{([^}]*)\}', s)}
    cm=[]
    for m in re.finditer(r'\{\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+), \.unicode_list = (\w+), .*?\.list_length = (\d+), \.type = (\w+)\}', s):
        rs, rl, gs, u, ll, t = m.groups()
        cm.append((int(rs), int(rl), int(gs), None if u=='NULL' else ul[int(u.split('_')[-1])], t))
    lh = int(re.search(r'\.line_height = (\d+)', s).group(1)); bl = int(re.search(r'\.base_line = (\d+)', s).group(1))
    return bitmap, gd, cm, lh, bl
def gid(cm, cp):
    for rs, rl, gs, ul, t in cm:
        rcp = cp - rs
        if rcp < 0 or rcp > rl: continue
        if ul is None: return gs + rcp
        if rcp in ul: return gs + ul.index(rcp)
    return 0
def render(path, text, out):
    bitmap, gd, cm, lh, bl = load(path)
    W = sum(gd[gid(cm,ord(c))][1] for c in text)//16 + 20
    img = Image.new('L', (W, lh+8), 0)
    x = 4; missing=[]
    for c in text:
        g = gid(cm, ord(c))
        if g == 0: missing.append(c); continue
        bi, adv, w, h, ox, oy = gd[g]
        if w and h:
            n = (w*h+1)//2
            raw = bitmap[bi:bi+n]
            px = []
            for b in raw: px += [b>>4, b&15]
            for yy in range(h):
                for xx in range(w):
                    X = int(x) + ox + xx; Y = 4 + (lh - bl) - oy - h + yy
                    if 0 <= X < W and 0 <= Y < lh+8: img.putpixel((X,Y), px[yy*w+xx]*17)
        x += adv/16
    img = img.resize((img.width*3, img.height*3), Image.NEAREST); img.save(out); return missing
if __name__ == '__main__':
    print('missing', render(sys.argv[1], sys.argv[2], sys.argv[3]))
