/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 */

#ifndef nsICharsetConverterManager_h___
#define nsICharsetConverterManager_h___

#include "nsString.h"
#include "nsError.h"
#include "nsISupports.h"
#include "nsIAtom.h"
#include "nsIUnicodeEncoder.h"
#include "nsIUnicodeDecoder.h"

#define NS_ICHARSETCONVERTERMANAGER_IID \
  {0x3c1c0161, 0x9bd0, 0x11d3, { 0x9d, 0x9, 0x0, 0x50, 0x4, 0x0, 0x7, 0xb2}}

// XXX change to NS_CHARSETCONVERTERMANAGER_CID
#define NS_ICHARSETCONVERTERMANAGER_CID \
  {0x3c1c0163, 0x9bd0, 0x11d3, { 0x9d, 0x9, 0x0, 0x50, 0x4, 0x0, 0x7, 0xb2}}

// XXX change to NS_CHARSETCONVERTERMANAGER_PID
#define NS_CHARSETCONVERTERMANAGER_PROGID "charset-converter-manager"

#define NS_REGISTRY_UCONV_BASE          "software/netscape/intl/uconv/"
// XXX change "xuconv" to "uconv" when the new enc&dec trees are in place
#define NS_DATA_BUNDLE_REGISTRY_KEY     "software/netscape/intl/xuconv/data/"
#define NS_TITLE_BUNDLE_REGISTRY_KEY    "software/netscape/intl/xuconv/titles/"

#define NS_ERROR_UCONV_NOCONV \
  NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_UCONV, 0x01)

/**
 * Interface for a Manager of Charset Converters.
 * 
 * This Manager's data is a cache of the stuff available directly through 
 * Registry and Extensible String Bundles. Plus a set of convenient APIs.
 * 
 * Note: The term "Charset" used in the classes, interfaces and file names 
 * should be read as "Coded Character Set". I am saying "charset" only for 
 * length considerations: it is a much shorter word. This convention is for 
 * source-code only, in the attached documents I will be either using the 
 * full expression or I'll specify a different convention.
 *
 * A DECODER converts from a random encoding into Unicode.
 * An ENCODER converts from Unicode into a random encoding.
 * All our data structures and APIs are divided like that.
 * However, when you have a charset data, you may have 3 cases:
 * a) the data is charset-dependet, but it is common for encoders and decoders
 * b) the data is different for the two of them, thus needing different APIs
 * and different "aProp" identifying it.
 * c) the data is relevant only for one: encoder or decoder; its nature making 
 * the distinction.
 *
 * @created         15/Nov/1999
 * @author  Catalin Rotaru [CATA]
 */
class nsICharsetConverterManager : public nsISupports
{
public:
  NS_DEFINE_STATIC_IID_ACCESSOR(NS_ICHARSETCONVERTERMANAGER_IID)

  NS_IMETHOD GetUnicodeEncoder(const nsString * aDest, 
      nsIUnicodeEncoder ** aResult) = 0;
  NS_IMETHOD GetUnicodeDecoder(const nsString * aSrc, 
      nsIUnicodeDecoder ** aResult) = 0;

  NS_IMETHOD GetDecoderList(nsString *** aResult, PRInt32 * aCount) = 0;
  NS_IMETHOD GetEncoderList(nsString *** aResult, PRInt32 * aCount) = 0;

  NS_IMETHOD GetCharsetData(nsString * aCharset, nsString * aProp, 
      nsString ** aResult) = 0;
  NS_IMETHOD GetCharsetTitle(nsString * aCharset, nsString ** aResult) = 0;
  NS_IMETHOD GetCharsetLangGroup(nsString * aCharset, nsIAtom ** aResult) = 0;

  NS_IMETHOD GetMIMEMailCharset(nsString * aCharset, nsString ** aResult) = 0;
  NS_IMETHOD GetMIMEHeaderEncodingMethod(nsString * aCharset, 
      nsString ** aResult) = 0;
};

#endif /* nsICharsetConverterManager_h___ */
