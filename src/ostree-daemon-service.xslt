<xsl:stylesheet version = '1.0'
     xmlns:xsl='http://www.w3.org/1999/XSL/Transform'>
  <xsl:output omit-xml-declaration="yes"/>
  <xsl:template match="/node">
    <xsl:text>[D-BUS Service]
Name=</xsl:text>
    <xsl:apply-templates /><xsl:text>
Exec=/usr/bin/ostree-daemon
User=root</xsl:text>
  </xsl:template>


  <xsl:template match="//interface[@name]">
    <xsl:variable name="if"><xsl:value-of select="@name"/></xsl:variable>
    <xsl:value-of select="$if"/><xsl:text></xsl:text>
  </xsl:template>

  <!-- this strips out all the non-tag text so that
       we don't emit lots of unwanted inter-tag whitespace  -->
  <xsl:template match="text()"/>

</xsl:stylesheet> 
