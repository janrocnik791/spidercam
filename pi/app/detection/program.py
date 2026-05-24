# from skimage.metrics import structural_similarity
import cv2
import numpy as np
import sys



# def method_detect_diff2(before, after, output_diff_path):

#   # Convert images to grayscale
#   before_gray = cv2.cvtColor(before, cv2.COLOR_BGR2GRAY)
#   after_gray = cv2.cvtColor(after, cv2.COLOR_BGR2GRAY)

#   # Compute SSIM between the two images
#   (score, diff) = structural_similarity(before_gray, after_gray, full=True)
#   print("Image Similarity: {:.4f}%".format(score * 100))

#   # The diff image contains the actual image differences between the two images
#   # and is represented as a floating point data type in the range [0,1] 
#   # so we must convert the array to 8-bit unsigned integers in the range
#   # [0,255] before we can use it with OpenCV
#   diff = (diff * 255).astype("uint8")
#   diff_box = cv2.merge([diff, diff, diff])

#   # Threshold the difference image, followed by finding contours to
#   # obtain the regions of the two input images that differ
#   thresh = cv2.threshold(diff, 0, 255, cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)[1]
#   contours = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
#   contours = contours[0] if len(contours) == 2 else contours[1]

#   mask = np.zeros(before.shape, dtype='uint8')
#   filled_after = after.copy()

#   for c in contours:
#     area = cv2.contourArea(c)
#     if area > 40:
#       x,y,w,h = cv2.boundingRect(c)
#       cv2.rectangle(before, (x, y), (x + w, y + h), (36,255,12), 2)
#       cv2.rectangle(after, (x, y), (x + w, y + h), (36,255,12), 2)
#       cv2.rectangle(diff_box, (x, y), (x + w, y + h), (36,255,12), 2)
#       cv2.drawContours(mask, [c], 0, (255,255,255), -1)
#       cv2.drawContours(filled_after, [c], 0, (0,255,0), -1)

#   cv2.imshow('before', before)
#   cv2.imshow('after', after)
#   cv2.imshow('diff', diff)
#   cv2.imshow('diff_box', diff_box)
#   cv2.imshow('mask', mask)
#   cv2.imshow('filled after', filled_after)
#   cv2.waitKey()

#   return diff


def align_image(gray1, gray2, img1, img2):
    # 2. Detect SIFT features and compute descriptors
  sift = cv2.SIFT_create()
  keypoints1, descriptors1 = sift.detectAndCompute(gray1, None)
  keypoints2, descriptors2 = sift.detectAndCompute(gray2, None)

  # 3. Match features using FLANN matcher
  FLANN_INDEX_KDTREE = 1
  index_params = dict(algorithm=FLANN_INDEX_KDTREE, trees=5)
  search_params = dict(checks=50)
  flann = cv2.FlannBasedMatcher(index_params, search_params)
  matches = flann.knnMatch(descriptors1, descriptors2, k=2)

  # Filter good matches using Lowe's ratio test
  good_matches = []
  for m, n in matches:
      if m.distance < 0.7 * n.distance:
          good_matches.append(m)

  if len(good_matches) < 4:
      raise ValueError("Not enough matching features found between images.")

  # 4. Extract coordinates of matched keypoints
  src_pts = np.float32([keypoints1[m.queryIdx].pt for m in good_matches]).reshape(-1, 1, 2)
  dst_pts = np.float32([keypoints2[m.trainIdx].pt for m in good_matches]).reshape(-1, 1, 2)

  # 5. Find Homography matrix and warp Image 1 to align with Image 2
  # RANSAC filters out bad, incorrect feature matches
  H, mask = cv2.findHomography(src_pts, dst_pts, cv2.RANSAC, 5.0)
  height, width, channels = img2.shape
  img1_warped = cv2.warpPerspective(img1, H, (width, height))

  return img1_warped

def method_detect_diff1(img1_warped, gray2, threshold_value=30):
  # Convert aligned image to grayscale for subtraction
  gray1_warped = cv2.cvtColor(img1_warped, cv2.COLOR_BGR2GRAY)

  # 6. Absolute subtraction to find differences
  raw_diff = cv2.absdiff(gray1_warped, gray2)

  # 7. Threshold and clean up the result
  # Ignore tiny pixel shifts and lighting changes; highlight major structural differences
  _, thresh_diff = cv2.threshold(raw_diff, threshold_value, 255, cv2.THRESH_BINARY)
  
  # Use morphology to remove tiny pixel noise
  kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
  cleaned_diff = cv2.morphologyEx(thresh_diff, cv2.MORPH_OPEN, kernel)

  return cleaned_diff


def detect_image_differences(img1_path, img2_path, output_diff_path):
  # 1. Load images in grayscale for processing, and color for visualization
  img1 = cv2.imread(img1_path)
  img2 = cv2.imread(img2_path)
  
  gray1 = cv2.cvtColor(img1, cv2.COLOR_BGR2GRAY)
  gray2 = cv2.cvtColor(img2, cv2.COLOR_BGR2GRAY)

  img1_warped = align_image(gray1, gray2, img1, img2)
  
  cv2.imwrite("warped_image1.jpg", img1_warped)

  diff_1 = method_detect_diff1(img1_warped, gray2, threshold_value=30)

  saveOutput(img1_path, img2_path, diff_1, output_diff_path)



def saveOutput(img1_path, img2_path, diff_image, output_diff_path):
  # Save the output image
  real_output_diff_path = output_diff_path[:-4] + img1_path[:-4] + "_and_" + img2_path[:-4] + output_diff_path[-4:]

  print(output_diff_path)
  print(real_output_diff_path)
  cv2.imwrite(real_output_diff_path, diff_image)
  print(f"Difference map successfully saved to: {real_output_diff_path}")
   

def main(image1path, image2path):
  # Run the function
  detect_image_differences(image1path, image2path, "./output/difference_result.jpg")




if '__main__' == __name__:
  main(sys.argv[1],sys.argv[2])
