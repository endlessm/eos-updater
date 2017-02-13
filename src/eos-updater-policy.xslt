<xsl:stylesheet version = '1.0'
     xmlns:xsl='http://www.w3.org/1999/XSL/Transform'>
  <xsl:output indent="yes"
              doctype-system="http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd"
              doctype-public="-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"/>
  <xsl:template match="/node">
    <xsl:variable name="dest">
      <xsl:value-of select="//interface[@name]/@name"/>
    </xsl:variable>
    <busconfig>
      <policy user="root">
        <xsl:comment> Allow root to own/send-to each interface </xsl:comment>
        <xsl:for-each select="//interface[@name]">
          <xsl:variable name="if"><xsl:value-of select="@name"/></xsl:variable>
          <allow>
            <xsl:attribute name="own">
              <xsl:value-of select="$if"/>
            </xsl:attribute>
          </allow>
          <allow>
            <xsl:attribute name="send_interface">
              <xsl:value-of select="$if"/>
            </xsl:attribute>
            <xsl:attribute name="send_destination">
              <xsl:value-of select="$dest"/>
            </xsl:attribute>
          </allow>
        </xsl:for-each>
        <xsl:comment> And the standard introspection interfaces </xsl:comment>
        <allow send_interface="org.freedesktop.DBus.Introspectable">
          <xsl:attribute name="send_destination">
            <xsl:value-of select="$dest"/>
          </xsl:attribute>
        </allow>
        <allow send_interface="org.freedesktop.DBus.Properties">
          <xsl:attribute name="send_destination">
            <xsl:value-of select="$dest"/>
          </xsl:attribute>
        </allow>
      </policy>
    </busconfig>
  </xsl:template>
</xsl:stylesheet> 
