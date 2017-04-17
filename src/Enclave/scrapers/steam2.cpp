//
// Copyright (c) 2016-2017 by Cornell University.  All Rights Reserved.
//
// Permission to use the "TownCrier" software ("TownCrier"), officially docketed at
// the Center for Technology Licensing at Cornell University as D-7364, developed
// through research conducted at Cornell University, and its associated copyrights
// solely for educational, research and non-profit purposes without fee is hereby
// granted, provided that the user agrees as follows:
//
// The permission granted herein is solely for the purpose of compiling the
// TowCrier source code. No other rights to use TownCrier and its associated
// copyrights for any other purpose are granted herein, whether commercial or
// non-commercial.
//
// Those desiring to incorporate TownCrier software into commercial products or use
// TownCrier and its associated copyrights for commercial purposes must contact the
// Center for Technology Licensing at Cornell University at 395 Pine Tree Road,
// Suite 310, Ithaca, NY 14850; email: ctl-connect@cornell.edu; Tel: 607-254-4698;
// FAX: 607-254-5454 for a commercial license.
//
// IN NO EVENT SHALL CORNELL UNIVERSITY BE LIABLE TO ANY PARTY FOR DIRECT,
// INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
// ARISING OUT OF THE USE OF TOWNCRIER AND ITS ASSOCIATED COPYRIGHTS, EVEN IF
// CORNELL UNIVERSITY MAY HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// THE WORK PROVIDED HEREIN IS ON AN "AS IS" BASIS, AND CORNELL UNIVERSITY HAS NO
// OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
// MODIFICATIONS.  CORNELL UNIVERSITY MAKES NO REPRESENTATIONS AND EXTENDS NO
// WARRANTIES OF ANY KIND, EITHER IMPLIED OR EXPRESS, INCLUDING, BUT NOT LIMITED
// TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR
// PURPOSE, OR THAT THE USE OF TOWNCRIER AND ITS ASSOCIATED COPYRIGHTS WILL NOT
// INFRINGE ANY PATENT, TRADEMARK OR OTHER RIGHTS.
//
// TownCrier was developed with funding in part by the National Science Foundation
// (NSF grants CNS-1314857, CNS-1330599, CNS-1453634, CNS-1518765, CNS-1514261), a
// Packard Fellowship, a Sloan Fellowship, Google Faculty Research Awards, and a
// VMWare Research Award.
//

#include <stdio.h>
#include <stdlib.h>
#include <string>
using std::string;

#include <vector>

#include "steam2.h"
#include "commons.h"

#include "external/picojson.h"

enum steam_error {
  INVALID = 0,
};

/* Handler function */
// return 0 -- Find no trade
//        1 -- Find a trade
err_code SteamScraper::handler(const uint8_t *req, size_t len, int *resp_data) {

  if (len != 6 * 32) {
    LL_CRITICAL("data_len %zu*32 is not 6*32", len / 32);
    return INVALID_PARAMS;
  }
  /*
  format[0] = encAPI[0];
  format[1] = encAPI[1];
  format[2] = ID_B;
  format[3] = T_B;
  format[4] = bytes32(LIST_I.length);
  format[5] = LIST_I[0];
  */

  uint32_t cutoff_time;
  size_t item_len;
  char _str_buf[100];

  // 0x00 .. 0x40
  // - encAPI:
  char encrypted_api_key[65] = {0};
  memcpy(encrypted_api_key, req, 0x40);
  LL_DEBUG("API key=%s", encrypted_api_key);

  // 0x40 .. 0x60 buyer_id
  char buyer_id[33] = {0};
  memcpy(buyer_id, req + 0x40, 0x20);
  LL_DEBUG("buyer id=%s", buyer_id);

  // 0x60 .. 0x80
  // get last 4 bytes
  cutoff_time = uint_bytes<uint32_t> (req + 0x60, 32);
  LL_DEBUG("cufoff time=%d", cutoff_time);


  // 0x80 .. 0xa0 - item_len
  item_len = uint_bytes<size_t> (req + 0x80, 32);
  LL_DEBUG("item_len=%zu", item_len);

  if (item_len * 32 != len - 0xa0) {
    LL_CRITICAL("item_len=%zu but rest data is %zu", item_len, len - 0xa0);
    return INVALID_PARAMS;
  }

  // 0xa0 .. 0xc0
  const char *items[item_len];
  for (size_t i = 0; i < item_len; i++) {
    items[i] = (char *) req + 0xa0 + 0x20 * i;
    LL_INFO("item: %s", items[i]);
  }

  return get_steam_transaction(items, item_len, buyer_id, cutoff_time, encrypted_api_key, resp_data);
}

// TODO: parse the response using a JSON / XML parser !
err_code SteamScraper::get_steam_transaction(const char **item_name_list,
                                             int item_list_len,
                                             const char *buyer_id,
                                             unsigned int time_cutoff,
                                             const char *api_key,
                                             int *resp) {
  if (item_name_list == NULL || buyer_id == NULL || api_key == NULL || resp == NULL) {
    return INVALID_PARAMS;
  }
  char tmp_req_time[12];
  snprintf(tmp_req_time, sizeof(tmp_req_time), "%u", time_cutoff);

  // reference query
  // https://api.steampowered.com/IEconService/GetTradeOffers/v0001/?get_sent_offers=1&get_received_offers=0&get_descriptions=0&active_only=1&historical_only=1&key=7978F8EDEF9695B57E72EC468E5781AD&time_historical_cutoff=1355220300
  string query =
      "/IEconService/GetTradeOffers/v0001/?get_sent_offers=0&get_received_offers=1&get_descriptions=0&active_only=1&historical_only=1&key="
          + string(api_key) + "&time_historical_cutoff=" + string(tmp_req_time);

  HttpRequest httpRequest("api.steampowered.com", query, true);
  HttpsClient httpClient(httpRequest);

  try {
    HttpResponse response = httpClient.getResponse();
    LL_DEBUG("response: %s", response.getContent().c_str());

    picojson::value _root_obj;
    string err_msg = picojson::parse(_root_obj, response.getContent());
    if (!err_msg.empty() || !_root_obj.is<picojson::object>()) {
      LL_CRITICAL("can't parse: %s", err_msg.c_str());
      return WEB_ERROR;
    }

    picojson::value _response_obj = _root_obj.get("response");
    if (!_response_obj.evaluate_as_boolean()) {
      LL_CRITICAL("Find no trade");
      *resp = 0;
    } else {
      // TODO: add more logic here
      *resp = 1;
    }
//    ret = parse_response(response.getContent().c_str(), buyer_id, item_name_list, item_list_len, api_key);
  }
  catch (std::runtime_error &e) {
    LL_CRITICAL("Https error: %s", e.what());
    LL_CRITICAL("Details: %s", httpClient.getError().c_str());
    httpClient.close();
    return WEB_ERROR;
  }
  return NO_ERROR;
}

char *SteamScraper::search(const char *buf, const char *search_string) {
  int len = strlen(buf);
  int slen = strlen((char *) search_string);
  char *temp = (char *) buf;
  if (slen > len) {
    return NULL;
  }
  while (strncmp(temp, search_string, slen) != 0) {
    temp += 1;
    if (temp == buf + len - slen - 1)
      return NULL;
  }
  return temp;
}

char *SteamScraper::get_next_trade_with_other(char *index, const char *other) {
  char *temp;
  char tempbuff[100];

  if (index == NULL) return NULL;
  if (other == NULL) return NULL;

  strncpy(tempbuff, "accountid_other\": \0", 19);
  strncat(tempbuff, other, sizeof tempbuff);

  temp = index + 1;
  temp = search(temp, tempbuff);
  return temp;
}

int SteamScraper::get_item_name(const char *key, char *appId, char *classId, char **resp) {
  int len, ret;
  char *end;
  char buf[16385];

  string query = "/ISteamEconomy/GetAssetClassInfo/v0001/?class_count=1&classid0=" + \
                string(classId) + \
                "&appid=" + \
                string(appId) + \
                "&key=" + \
                string(key) + \
                " HTTP/1.1";

  HttpRequest httpRequest("api.steampowered.com", query, this->headers);
  HttpsClient httpClient(httpRequest);
  char *query1;
  try {
    HttpResponse response = httpClient.getResponse();
    query1 = search(response.getContent().c_str(), "\"name\": \"");

  }
  catch (std::runtime_error &e) {
    LL_CRITICAL("Https error: %s", e.what());
    LL_CRITICAL("Details: %s", httpClient.getError().c_str());
    httpClient.close();
    return -1;
  }
  if (query1 == NULL) {
    return -1;
  }
  //Safe?
  query1 += 9;
  end = search(query1, "\"");
  if (end == NULL) {
    return -1;
  }

  len = end - query1;
  *resp = (char *) malloc(len + 1);
  memcpy(*resp, query1, len);
  (*resp)[len] = 0;
  return 0;
}

int SteamScraper::in_list(const char **list, int len, const char *name) {
  int i;
  int nlen = strlen(name);
  for (i = 0; i < len; i++) {
    if (strncmp(list[i], name, nlen) == 0) return 1;
  }
  return -1;
}

int SteamScraper::parse_response(const char *resp, const char *other, const char **listB, int lenB, const char *key) {
  int ret, counter, flag;
  char *index = (char *) resp;
  char *name;
  char *temp;
  char *end, *end2;
  char appId[21];
  char classId[21];

  index = get_next_trade_with_other(index, other);
  temp = index;
  while (temp != NULL) {
    end = search(temp, "confirmation_method");
    if (end == NULL) {
      return -1;
    }
    temp = search(temp, "trade_offer_state");
    if (temp == NULL) {
      return -1;
    }
    temp += 20;
    end2 = search(temp, ",");
    if (end2 == NULL || end2 > end) {
      return -1;
    }
    if (strncmp(temp, "3", 1) != 0) {
      index = get_next_trade_with_other(index, other);
      temp = index;
      continue;
    }
    temp = search(temp, "items_to_give");
    if (temp == NULL) {
      return -1;
    }
    if (temp > end) {
      index = get_next_trade_with_other(index, other);
      temp = index;
      continue;
    }
    temp = search(temp, "appid\": \"");
    if (temp == NULL) {
      return -1;
    }

    temp += 9;
    /* for all items in this trade */
    counter = 0;
    flag = 0;
    while (temp != NULL && temp < end) {
      end2 = search(temp, "\"");
      if (end2 == NULL) {
        return -1;
      }
      strncpy(appId, temp, (int) (end2 - temp));
      appId[(int) (end2 - temp)] = 0;

      temp = search(temp, "classid\": \"");
      if (temp == NULL) {
        return -1;
      }
      temp += 11;
      end2 = search(temp, "\"");
      if (end2 == NULL) {
        return -1;
      }
      strncpy(classId, temp, (int) (end2 - temp));
      classId[(int) (end2 - temp)] = 0;

      /* get name by 2nd request */
      ret = get_item_name(key, appId, classId, &name);
      if (ret < 0) {
        return -1;
      }
      ret = in_list(listB, lenB, name);
      free(name);
      if (ret < 0) {
        flag = 1;
        break;
      }
      counter += 1;

      /* continue loop */
      temp = search(temp, "appid\": \"");
    }
    /* we found a matching trade */
    if (counter == lenB && flag == 0) {
      return 0;
    }
    /* continue outer loop over all trades */
    index = get_next_trade_with_other(index, other);
    temp = index;
  }
  /*printf("SEARCHED ALL TRADES: NOT FOUND\n");*/
  return -1;
}


//
///*
//int main(int argc, char* argv[]) {
//    int rc, ret;
//    char * listB[1] = {"Portal"};
//    char * test2[2] = {"Dark Ranger's Headdress", "Death Shadow Bow"};
//    printf("Starting...\n");
//    // needs as input time of request, T_B time for response, key, account # ID_B, list of items L_B, account # ID_S
//    // I'm not sure how we get time... but so long as we get it somehow...
//    rc = get_steam_transaction(listB, 1, "32884794", 1456380265, "7978F8EDEF9695B57E72EC468E5781AD", &ret);
//    printf("%d, %d\n", rc, ret);
//    //rc should be 0, ret should be 1
//
//    return 0;
//}
//
//*/
