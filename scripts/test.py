#!/usr/bin/env python
from __future__ import print_function

import roslib
import sys
import rospy
import cv2
from std_msgs.msg import String
from sensor_msgs.msg import Image
from cv_bridge import CvBridge, CvBridgeError
import numpy as np


fourcc = cv2.VideoWriter_fourcc('m','p','4','v')
video = cv2.VideoWriter('opencv.mp4', fourcc, 5.0, (640, 480))

def calc_cy(M):
    try:
        cy = int(M['m01']/M['m00'])
    except KeyError:
        cy = 0
    return cy

class image_converter:

  def __init__(self):
    self.image_pub = rospy.Publisher("image",Image)

    self.bridge = CvBridge()
    self.image_sub = rospy.Subscriber("/usb_cam/image_raw",Image,self.callback)

  def callback(self,data):
    try:
      cv_image = self.bridge.imgmsg_to_cv2(data, "bgr8")
    except CvBridgeError as e:
      print(e)


    #cv2.imwrite("original.png", cv_image)

    #gray scale, histogram equalization
    gray_image = cv2.cvtColor(cv_image, cv2.COLOR_BGR2GRAY)
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
    cl_image = clahe.apply(gray_image)

    #cv2.imwrite("gray.png", cl_image)

    #blur
    cv_image2 = cv2.GaussianBlur(cl_image,(3,3),0)
    cv_image2 = cv2.medianBlur(cv_image2,3)

    #opening
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
    open_image = cv2.morphologyEx(cv_image2, cv2.MORPH_OPEN,kernel, iterations = 3)

    #cv2.imwrite("blur.png", open_image)

    #binarize
    ret, thresh = cv2.threshold(open_image,160,255,cv2.THRESH_BINARY)
    #cv2.imwrite("thresh.png", thresh)

    #find contours
    image, contours, hierarchy = cv2.findContours(thresh,cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_NONE)
    cv_image = cv2.drawContours(cv_image,contours,-1,(0,255,0),3)
    #cv2.imwrite("detection1.png", cv_image)
    
    #bounding rectangle
    rects = list(map(cv2.minAreaRect, contours))
    boxs = list(map(cv2.boxPoints, rects))
    boxs = list(map(np.int0, boxs))
    boxareas = list(map(cv2.contourArea, boxs))
    areas = list(map(cv2.contourArea, contours))
    M = list(map(cv2.moments, contours))
    cy = list(map(calc_cy, M))

    cv_image = cv2.drawContours(cv_image,boxs,-1,(255,0,0),2)
    #cv2.imwrite("detection2.png", cv_image)

    #detection
    for i in range(len(contours)):
        w, h = rects[i][1]
        if boxareas[i] != 0 and w != 0 and h != 0:
            if (((float(w) / h) < 0.4) or ((float(h) / w) < 0.4)) and boxareas[i] > 28000:
               # if boxareas[i] * 0.78 < areas[i]:
                    #if cy[i] > 240:
                        cv_image = cv2.drawContours(cv_image,[boxs[i]],0,(0,0,255),2)
                        #print("area = " + str(areas[i]))
                        #print("boxareas = " + str(boxareas[i]))
                        #print("w/h=" + str(float(w) / h))
                        #print("h/w=" + str(float(h) / w))
                        #print("area / box = " + str(areas[i] / boxareas[i])) 
                        detection = True
    
    try:
      self.image_pub.publish(self.bridge.cv2_to_imgmsg(cv_image, "bgr8"))
    except CvBridgeError as e:
      print(e)

    #cv2.imwrite("detection3.png", cv_image)

    video.write(cv_image)

    #cv2.imshow("open_img", open_image)
    #cv2.imshow("thresh", thresh)
    #cv2.imshow("detection", cv_image)
    #cv2.imshow("gray", gray_image)
    #cv2.imshow("clahe",cl_image)

    cv2.waitKey(3)


def main(args):
  ic = image_converter()
  rospy.init_node('image_converter', anonymous=True)
  try:
    rospy.spin()
  except KeyboardInterrupt:
    print("Shutting down")
  video.release()
  cv2.destroyAllWindows()

if __name__ == '__main__':
    main(sys.argv)

