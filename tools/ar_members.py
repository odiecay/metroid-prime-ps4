"""Read ordinary GNU/BSD ar payload identities without extracting any files."""
import hashlib
from pathlib import Path

def member_hashes(path):
    data=Path(path).read_bytes()
    if data[:8]!=b'!<arch>\n':raise ValueError('Expected ordinary native archive')
    cursor=8;names=b'';result={}
    while cursor<len(data):
        head=data[cursor:cursor+60]
        if len(head)!=60 or head[58:60]!=b'`\n':raise ValueError('Invalid archive member header')
        size=int(head[48:58]);start=cursor+60;payload=data[start:start+size]
        if len(payload)!=size:raise ValueError('Truncated archive member')
        cursor=start+size+(size%2);name=head[:16].decode().strip()
        if name=='//':names=payload;continue
        if name in ('/','/SYM64/'):continue
        if name.startswith('#1/'):
            length=int(name[3:]);name=payload[:length].rstrip(b'\0').decode();payload=payload[length:]
        elif name.startswith('/'):
            offset=int(name[1:]);end=names.index(b'/\n',offset);name=names[offset:end].decode()
        else:name=name.removesuffix('/')
        if name in result:raise ValueError('Duplicate archive member: '+name)
        result[name]=hashlib.sha256(payload).hexdigest()
    return result
