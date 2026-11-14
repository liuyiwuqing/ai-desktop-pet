#!/usr/bin/env python3
"""Actual Vue with isolated RAM list/export HTTP; no CORE, DB, Broker or device."""
import argparse
import copy
import csv
import functools
import http.server
import io
import json
import threading
import time
from datetime import datetime
from urllib.parse import parse_qs, urlparse
from management_ui_fixture import Fixture, TOKEN, CONTROL


def initial():
    rows=[]
    for i in range(34):
        stamp='2026-11-14T08:00:00Z' if i<32 else '2026-11-14T09:00:00Z'
        rows.append({'messageId':'metadata-'+str(i),'connectorId':'fixture','source':'manual' if i<31 or i==32 else 'halo',
            'eventType':'manual.test','title':'范围消息 '+str(i),'content':'不导出正文','summary':'筛选页面回归','sender':'不导出发送者','originalUrl':'',
            'priority':'normal','receivedAt':stamp,'createdAt':'2020-01-01T00:00:00Z','expiresAt':'2026-11-15T09:00:00Z',
            'readAt':None if i<33 else stamp,'readStatus':'unread' if i<33 else 'read','dataClass':'internal'})
    return {'queryRows':rows,'queryDeliveries':[{'eventId':'event-'+str(i),'messageId':row['messageId'],'deviceId':'device-a' if i<31 else 'device-b',
        'title':row['title'],'status':'failed' if i<32 else 'pending','attempts':1,'lastError':'ack_timeout','nextAttemptAt':'2026-11-15T09:00:00Z',
        'readAt':None,'messageReceivedAt':row['receivedAt']} for i,row in enumerate(rows)],
        'queryRequests':[],'exportRequests':[],'queryError':'','queryDelay':0,'exportMode':'success','exportDelay':0}


class MetadataFixture(Fixture):
    state={**copy.deepcopy(Fixture.state),**initial()}

    def do_POST(self):
        if urlparse(self.path).path!='/__qa/query':return super().do_POST()
        if self.headers.get('X-QA-Key')!=CONTROL:return self.json_response(401,{'error':'unauthorized'})
        data=json.loads(self.rfile.read(int(self.headers.get('Content-Length','0'))))
        with self.lock:
            if data.get('reset'):self.state.update(initial())
            for key in ('queryError','queryDelay','exportMode','exportDelay'):
                if key in data:self.state[key]=data[key]
            if data.get('empty'):self.state.update(queryRows=[],queryDeliveries=[])
        self.json_response(200,{'ok':True})

    def do_GET(self):
        path=urlparse(self.path).path
        if path not in ('/api/v1/messages','/api/v1/deliveries','/api/v1/messages/export','/api/v1/deliveries/export'):
            return super().do_GET()
        if self.headers.get('Authorization')!='Bearer '+TOKEN:return self.json_response(401,{'error':'unauthorized'})
        is_message=path.startswith('/api/v1/messages'); export=path.endswith('/export')
        query=parse_qs(urlparse(self.path).query)
        with self.lock:
            self.state['exportRequests' if export else 'queryRequests'].append(self.path)
            data=copy.deepcopy(self.state)
        delay=data['exportDelay'] if export else data['queryDelay']
        if delay:time.sleep(delay)
        if data['queryError']==('messages' if is_message else 'deliveries') and not export:
            return self.json_response(503,{'error':'unavailable'})
        if export and data['exportMode']!='success':
            mode=data['exportMode']
            if mode=='invalid':return self.json_response(200,{'wrong':'format'})
            return self.json_response(401 if mode=='unauthorized' else 409 if mode=='large' else 503,
                {'error':'unauthorized' if mode=='unauthorized' else 'export_too_large' if mode=='large' else 'unavailable'})
        rows=data['queryRows' if is_message else 'queryDeliveries']
        for parameter,key in [('readStatus','readStatus'),('source','source')] if is_message else [('status','status'),('deviceId','deviceId')]:
            value=query.get(parameter,[''])[0]
            if value:rows=[row for row in rows if row[key]==value]
        for parameter,lower in [('receivedFrom',True),('receivedBefore',False)]:
            value=query.get(parameter,[''])[0]
            if value:
                boundary=datetime.fromisoformat(value.replace('Z','+00:00'))
                def within(row):
                    stamp=datetime.fromisoformat(row['receivedAt' if is_message else 'messageReceivedAt'].replace('Z','+00:00'))
                    return stamp>=boundary if lower else stamp<boundary
                rows=[row for row in rows if within(row)]
        if export:
            out=io.StringIO(newline='');writer=csv.writer(out,quoting=csv.QUOTE_ALL,lineterminator='\r\n')
            columns=['messageId','connectorId','source','eventType','priority','receivedAt','createdAt','expiresAt','readAt','readStatus','contentPurgedAt'] if is_message else ['eventId','messageId','deviceId','status','attempts','messageReceivedAt','receivedAt','displayedAt','readAt','nextAttemptAt']
            writer.writerow(columns);writer.writerows([[row.get(column,'') or '' for column in columns] for row in rows])
            payload=('\ufeff'+out.getvalue()).encode()
            self.send_response(200);self.send_header('Content-Type','text/csv;charset=UTF-8');self.send_header('Cache-Control','no-store')
            self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload);return
        page=int(query.get('page',['1'])[0]);size=int(query.get('size',['30'])[0])
        self.json_response(200,{'items':rows[(page-1)*size:page*size],'total':len(rows),'page':page,'size':size})


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--frontend',required=True);parser.add_argument('--port',type=int,default=18812)
    args=parser.parse_args()
    server=http.server.ThreadingHTTPServer(('127.0.0.1',args.port),functools.partial(MetadataFixture,directory=args.frontend))
    print('UI boundary fixture ready',flush=True);server.serve_forever()
