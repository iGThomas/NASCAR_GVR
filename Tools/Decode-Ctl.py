import re,sys
DIK={1:'Esc',2:'1',3:'2',4:'3',5:'4',6:'5',7:'6',8:'7',9:'8',10:'9',11:'0',12:'-',13:'=',14:'Backspace',15:'Tab',16:'Q',17:'W',18:'E',19:'R',20:'T',21:'Y',22:'U',23:'I',24:'O',25:'P',26:'[',27:']',28:'Enter',29:'LCtrl',30:'A',31:'S',32:'D',33:'F',34:'G',35:'H',36:'J',37:'K',38:'L',39:';',40:"'",41:'`',42:'LShift',43:'Backslash',44:'Z',45:'X',46:'C',47:'V',48:'B',49:'N',50:'M',51:',',52:'.',53:'/',54:'RShift',55:'Num*',56:'LAlt',57:'Space',58:'CapsLock',59:'F1',60:'F2',61:'F3',62:'F4',63:'F5',64:'F6',65:'F7',66:'F8',67:'F9',68:'F10',69:'NumLock',70:'ScrollLock',71:'Num7',72:'Num8',73:'Num9',74:'Num-',75:'Num4',76:'Num5',77:'Num6',78:'Num+',79:'Num1',80:'Num2',81:'Num3',82:'Num0',83:'Num.',87:'F11',88:'F12',89:'(unbound)',156:'NumEnter',157:'RCtrl',181:'Num/',183:'PrintScr',184:'RAlt',197:'Pause',199:'Home',200:'Up',201:'PgUp',203:'Left',205:'Right',207:'End',208:'Down',209:'PgDn',210:'Insert',211:'Delete'}
def name(dev,code):
    if dev==0: return DIK.get(code,f'key#{code}')
    return f'controller{dev} input {code}'
for path in sys.argv[1:]:
    print('=====',path)
    for line in open(path,encoding='latin1'):
        m=re.match(r'Control - (.+?)="\((\d+),\s*(-?\d+)\)"',line.strip())
        if m:
            n,dev,code=m.group(1),int(m.group(2)),int(m.group(3))
            if dev==0 and code==89: continue
            print(f'  {n:34} {name(dev,code)}')
